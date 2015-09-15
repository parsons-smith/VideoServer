#include "BasicHashTable.hh"
#include "tinyxml.h"
#include "tinystr.h"
#include "sys_os.h"
#include "MutexQueue.cpp"
#include <opencv/cv.h>
#include <opencv2/core/types_c.h>
#include <opencv2/core/gpumat.hpp>
#include <opencv2/core/operations.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/contrib/contrib.hpp>
#include <opencv2/video/background_segm.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <stdio.h>
#include <string.h>
#include <opencv/cxcore.h>
#include <opencv/highgui.h>
#include <stdlib.h>
#include <exception>
#include <iostream>
using namespace std;
using namespace cv; 
const double MHI_DURATION = 0.5;
const double MAX_TIME_DELTA = 0.5;
const double MIN_TIME_DELTA = 0.05;
const int N = 3;
const double BINARYTHRES=30;
const int RECT_MAX_AERA = 100;//���С��RECT_MAX_AERA��rect������
const int interval = 5;
int cms_fd = -1;
HashTable * videoSession = HashTable::create(0); //�����洢VIDEOSESSION��Hash��
MutexQueue<char *> wmsgq;

#if __WIN32_OS__
	#include <winsock2.h>
	#pragma comment(lib, "ws2_32.lib")
#endif


#define MAXDATASIZE 4096
#define CMS_SERVER_IP "127.0.0.1"
#define CMS_SERVER_PORT 8000
#define MAXCONN_NUM 10
int numbytes,sock_fd;
struct sockaddr_in server_addr; 
char buf[MAXDATASIZE]; 


struct vsession{
	char mac[100];
	char cfd[10];
	char rtspurl[200];
	int startx;
	int starty;
	int endx;
	int endy; 
	char warning_type[20];
};

class VideoUnderstanding{
public:
	IplImage **buf,*mhi, *motion, *image;
	CvPoint pt[4];
	CvCapture *capture;
	int last;
	struct vsession *vs;
	pthread_t rpt;
	struct tm *start,*end;
	int flag;
	void *x_lock;
public:
	VideoUnderstanding(struct vsession * v);
	~VideoUnderstanding();
	int PointAtLineLeftRight(Point ptStart, Point ptEnd, Point ptTest);
	bool IsTwoLineIntersect(Point ptLine1Start, Point ptLine1End, Point ptLine2Start, Point ptLine2End);
	bool IsLineIntersectRect(Point ptStart, Point ptEnd, Rect rect);
	void update_mhi(IplImage *img, IplImage *dst, int diff_threshold);
	int run();
	int stop();
	int sendwarningmessage(const char * type);
	int sendstartreply(const char * msg);
};

void *run_thread(void * lw);
void cleanup(void * arg);
int DecodeXml(char * buffer);
void * send_thread(void * st);

VideoUnderstanding::VideoUnderstanding(struct vsession * v){
	vs = v;
	flag = 0;
	buf=0;
	mhi =0;
	motion=0;
	last = 0;
	x_lock = sys_os_create_mutex();
	sys_os_mutex_enter(x_lock);
}

VideoUnderstanding::~VideoUnderstanding(){
	delete vs;
}

int VideoUnderstanding::PointAtLineLeftRight(Point Start, Point End, Point Test)
{
	Start.x -= Test.x;
	Start.y -= Test.y;
	End.x -= Test.x;
	End.y -= Test.y;
	int nRet = Start.x * End.y - Start.y * End.x;
	if (nRet == 0)
		return 0;
	else if (nRet > 0)
		return 1;
	else if (nRet < 0)
		return -1;
	return 0;
}

bool VideoUnderstanding::IsTwoLineIntersect(Point ptLine1Start, Point ptLine1End, Point ptLine2Start, Point ptLine2End)
{
	int nLine1Start = PointAtLineLeftRight(ptLine2Start, ptLine2End, ptLine1Start);
	int nLine1End = PointAtLineLeftRight(ptLine2Start, ptLine2End, ptLine1End);
	if (nLine1Start * nLine1End > 0)
		return FALSE;
	int nLine2Start = PointAtLineLeftRight(ptLine1Start, ptLine1End, ptLine2Start);
	int nLine2End = PointAtLineLeftRight(ptLine1Start, ptLine1End, ptLine2End);
	if (nLine2Start * nLine2End > 0)
		return FALSE;
	return TRUE;
}

bool VideoUnderstanding::IsLineIntersectRect(Point Start, Point End, Rect rect)
{
	// Two point both are in rect
	if (rect.contains(Start) && rect.contains(End))
		return TRUE;
	// One point is in rect, another not.
	if (rect.contains(Start) && !rect.contains(End))
		return TRUE;
	if (!rect.contains(Start) && rect.contains(End))
		return TRUE;
	// Two point both aren't in rect
	if (IsTwoLineIntersect(Start, End, Point(rect.x, rect.y), Point(rect.x+rect.width, rect.y)))
		return TRUE;
	if (IsTwoLineIntersect(Start, End, Point(rect.x+rect.width, rect.y), Point(rect.x+rect.width, rect.y-rect.height)))
		return TRUE;
	if (IsTwoLineIntersect(Start, End, Point(rect.x+rect.width, rect.y-rect.height),Point(rect.x, rect.y-rect.height)))
		return TRUE;
	if (IsTwoLineIntersect(Start, End, Point(rect.x, rect.y-rect.height),Point(rect.x, rect.y)))
		return TRUE;
	return FALSE;
}

void  VideoUnderstanding::update_mhi( IplImage* img, IplImage* dst, int diff_threshold )
{
	double timestamp = clock()/100.; // get current time in seconds
	CvSize size = cvSize(img->width,img->height); // get current frame size
	int i, idx1, idx2;
	IplImage* silh;
	//IplImage *img1;
	IplImage* pyr = cvCreateImage( cvSize((size.width & -2)/2, (size.height & -2)/2), 8, 1 );
	CvMemStorage *stor;
	CvSeq *cont, *result;

	if( !mhi || mhi->width != size.width || mhi->height != size.height ) {
		if( buf == 0 ) {
			buf = (IplImage**)malloc(N*sizeof(buf[0]));
			memset( buf, 0, N*sizeof(buf[0]));
		}
		for( i = 0; i < N; i++ ) {
			cvReleaseImage( &buf[i] );
			buf[i] = cvCreateImage( size, IPL_DEPTH_8U, 1 );
			cvZero( buf[i] );
		}
		cvReleaseImage( &mhi );
		mhi = cvCreateImage( size, IPL_DEPTH_32F, 1 );
		cvZero( mhi ); // clear MHI at the beginning
	} // end of if(mhi)

	cvCvtColor( img, buf[last], CV_BGR2GRAY ); // convert frame to grayscale
	idx1 = last;
	idx2 = (last + 1) % N; // index of (last - (N-1))th frame 
	last = idx2;
	// ��֡��
	silh = buf[idx2];
	cvAbsDiff( buf[idx1], buf[idx2], silh ); // get difference between frames 
	// �Բ�ͼ������ֵ��
	cvThreshold( silh, silh, BINARYTHRES, 255, CV_THRESH_BINARY ); // and threshold it
	cvUpdateMotionHistory( silh, mhi, timestamp, MHI_DURATION ); // update MHI
	cvCvtScale( mhi, dst, 255./MHI_DURATION, 
	  (MHI_DURATION - timestamp)*255./MHI_DURATION );	
	cvCvtScale( mhi, dst, 255./MHI_DURATION, 0 );	
	// ��ֵ�˲�������С������
	cvSmooth( dst, dst, CV_MEDIAN, 3, 0, 0, 0 );
	// ���²�����ȥ������
	cvPyrDown( dst, pyr, 7 );
	cvDilate( pyr, pyr, 0, 1 );  // �����Ͳ���������Ŀ��Ĳ������ն�
	cvPyrUp( pyr, dst, 7 );
	//
	// ����ĳ���������ҵ�����
	//
	// Create dynamic structure and sequence.
	stor = cvCreateMemStorage(0);
	cont = cvCreateSeq(CV_SEQ_ELTYPE_POINT, sizeof(CvSeq), sizeof(CvPoint) , stor);
	result = cvCreateSeq(CV_SEQ_ELTYPE_POINT, sizeof(CvSeq), sizeof(CvPoint) , stor);//jm+
	// �ҵ���������
	cvFindContours( dst, stor, &cont, sizeof(CvContour),CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, cvPoint(0,0));

	//cvCvtColor(img, img1, CV_BGR2GRAY);
	int num[16];
	for (int i = 0; i < 16; i++)
		num[i] = 0;
	for (int i = 1; i <= img->height;i++)
		for (int j = 1; j <= img->width; j++){
			int x = CV_IMAGE_ELEM(img, uchar, i, j);
			x = x/16;
			num[x]++;
		}


	int tmp = img->height * img->width / 2;
	if(vs->warning_type[0] == '1'){
		for(;cont;cont = cont->h_next){
			Rect r = ((CvContour*)cont)->rect;
			// ֱ��ʹ��CONTOUR�еľ�����������
			//���С��RECT_MAX_AERA��rect������
			if (r.height * r.width > RECT_MAX_AERA){
				cvRectangle( img, cvPoint(r.x,r.y), cvPoint(r.x + r.width, r.y + r.height),CV_RGB(255,0,0), 1, CV_AA,0);
				if(IsLineIntersectRect(Point(vs->startx, vs->starty) , Point(vs->endx, vs->endy),r)){
					end = localtime((time_t *)sys_os_get_ms());
					if(end->tm_sec - start->tm_sec >= interval){
						sendwarningmessage("cross");
						cout << "WARNING: "<<time(NULL) << " "<<vs->rtspurl<<" cross!"<<endl;
						start = localtime((time_t *)sys_os_get_ms());
					}
				}
			  }
		}
	}
	if(vs->warning_type[1] == '1'){
		for (int i = 0; i < 16; i++){
			if (num[i] >= tmp){
				cout << "WARNING: "<< time(NULL) <<" "<<vs->rtspurl<<" shelter!"<<endl;
				sendwarningmessage("shelter");
				break;
			}
		}
	}

	cvReleaseMemStorage(&stor);
	cvReleaseImage( &pyr );
}

int VideoUnderstanding::run()
{
	int lthread = sys_os_create_thread(run_thread, this);
	if(lthread < 0){
		printf("run_thread create failed!\n");
		return -1;
	}
	return 0;
}

int VideoUnderstanding::stop(){
	sys_os_mutex_leave(x_lock);
#if __LINUX_OS__
	pthread_join(rpt, NULL);
#elif __WIN32_OS__

#endif

	/*
	cvDestroyWindow( "Motion" );
	if(capture != NULL){
		cvReleaseCapture(&capture);
	}
	*/
	return 0;
}

int VideoUnderstanding::sendwarningmessage(const char * type){
	const char * msgstl = "<?xml version=\"1.0\"?><Envelope type=\"warning\"><profile><mac>%s</mac><cfd>%s</cfd><rtspuri>%s</rtspuri><time>%ld</time><type>%s</type></profile></Envelope>";
	char * replybuffer = (char *)malloc(sizeof(char) * MAXDATASIZE);
	memset(replybuffer,0, strlen(replybuffer));
	int offset = sprintf(replybuffer, msgstl, vs->mac, vs->cfd, vs->rtspurl,time(NULL), type);
	replybuffer[offset] = '\0';
	wmsgq.push(replybuffer);
	return 0;
}

int VideoUnderstanding::sendstartreply(const char * msg){
	const char * msgstl = "<?xml version=\"1.0\"?><Envelope type=\"r_startdeal\"><profile><mac>%s</mac><cfd>%s</cfd><rtspuri>%s</rtspuri><action>%s</action></profile></Envelope>";
	char * replybuffer = (char *)malloc(sizeof(char) * MAXDATASIZE);
	memset(replybuffer,0, strlen(replybuffer));
	sprintf(replybuffer, msgstl, vs->mac, vs->cfd, vs->rtspurl, msg);
	wmsgq.push(replybuffer);
	return 0;
}

void *send_thread(void * st){
	while(true){
		usleep(10);
		while(!wmsgq.empty()){
			usleep(10);
			char *msg = wmsgq.pop();
			if(send(sock_fd, msg, strlen(msg), 0) == -1){
				perror("ERROR:Send error\n");
			}
			delete [] msg;
		}
	}
}


void *run_thread(void * lw){
	((VideoUnderstanding*)lw)->capture = cvCreateFileCapture( ((VideoUnderstanding*)lw)->vs->rtspurl);
	//pthread_cleanup_push(cleanup, lw);
	if( ((VideoUnderstanding*)lw)->capture == NULL ){
		printf("ERROR: Connection failed...\n");
		((VideoUnderstanding*)lw)->sendstartreply("fail");
		sys_os_thread_exit();
		return (void*)-1;
	}else{
		((VideoUnderstanding*)lw)->sendstartreply("success");
		videoSession->Add(((VideoUnderstanding*)lw)->vs->rtspurl, (void *)lw);
	}
	try{
		if(((VideoUnderstanding*)lw)->capture ){
			//cvNamedWindow( "Motion", 1 );
			//gettimeofday(& ((VideoUnderstanding*)lw)->start,NULL);
			((VideoUnderstanding*)lw)->start = localtime((time_t *)sys_os_get_ms());
			for(;sys_os_sig_wait_timeout(((VideoUnderstanding*)lw)->x_lock, 1);){
				if( !cvGrabFrame( ((VideoUnderstanding*)lw)->capture ))  break;
				((VideoUnderstanding*)lw)->image=cvQueryFrame( ((VideoUnderstanding*)lw)->capture );//�ô˾䲻����
				((VideoUnderstanding*)lw)->image->origin=0;
				if( ((VideoUnderstanding*)lw)->image ){
					if( ! ((VideoUnderstanding*)lw)->motion ){
						((VideoUnderstanding*)lw)->motion = cvCreateImage( cvSize(((VideoUnderstanding*)lw)->image->width,((VideoUnderstanding*)lw)->image->height), 8, 1 );
						cvZero(((VideoUnderstanding*)lw)->motion );
						((VideoUnderstanding*)lw)->motion->origin = ((VideoUnderstanding*)lw)->image->origin;
					}
				}
				((VideoUnderstanding*)lw)->update_mhi( ((VideoUnderstanding*)lw)->image,  ((VideoUnderstanding*)lw)->motion, 60 );
				//printf("-");
				//cvLine( ((VideoUnderstanding*)lw)->image, Point(((VideoUnderstanding*)lw)->vs->startx, ((VideoUnderstanding*)lw)->vs->starty), Point(((VideoUnderstanding*)lw)->vs->endx, ((VideoUnderstanding*)lw)->vs->endy),Scalar(255,0,0));   //����������
				//cvShowImage( "Motion", ((VideoUnderstanding*)lw)->image );
				if( cvWaitKey(10) >= 0 ) break;
			}
			cvReleaseCapture( & ((VideoUnderstanding*)lw)->capture );
			//cvDestroyWindow( "Motion" );
		}
	}catch(exception &e){
		//exception while running
		printf("ERROR: Exception %s \n",e.what());
		((VideoUnderstanding*)lw)->sendstartreply("exception");
		videoSession->Remove(((VideoUnderstanding*)lw)->vs->rtspurl);
	}
	//pthread_cleanup_pop(0);
	sys_os_thread_exit();
	return (void*)0;
}


int main(){
	printf("INFO:VideoServer  started...\n");

#if __WIN32_OS__
	WSADATA  Ws;
	if ( WSAStartup(MAKEWORD(2,2), &Ws) != 0 )
	{
		cout<<"Init Windows Socket Failed::"<<GetLastError()<<endl;
		return -1;
	}
#endif

	if ((sock_fd = socket ( AF_INET , SOCK_STREAM , 0)) == - 1) { 
			perror ("ERROR:Socket error\n"); 
			return -1;
	} 
	memset ( &server_addr, 0, sizeof(struct sockaddr)); 
	server_addr.sin_family = AF_INET; 
	server_addr.sin_port = htons (CMS_SERVER_PORT); 
	server_addr.sin_addr.s_addr = inet_addr(CMS_SERVER_IP); 
	if ( connect ( sock_fd, ( struct sockaddr * ) & server_addr, sizeof( struct sockaddr ) ) == -1) { 
		perror ("ERROR:Cannot connect to a CMSServer...\n"); 
		return -1;
	}
	const char * registermsg = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?><Envelope type=\"vregister\"></Envelope>";
	if ( send ( sock_fd, registermsg , strlen(registermsg), 0) == - 1) { 
		perror ( "ERROR:Send error\n" ); 
	}
	fd_set fdsr;
	int ret;
	struct timeval tv;
	int sthread = sys_os_create_thread(send_thread, NULL);
	if(sthread < 0){
		printf("ERROR:send_thread create failed!\n");
	}
	while(1){
		tv.tv_sec = 30;
		tv.tv_usec = 0;
		FD_ZERO(&fdsr);
		FD_SET(sock_fd, &fdsr);
		ret = select(sock_fd + 1, &fdsr, NULL, NULL, &tv);
		if (ret < 0) {
			perror("ERROR:Select...\n");
			continue;
		} 
		if (FD_ISSET(sock_fd, &fdsr)) {
			ret = recv(sock_fd, buf, MAXDATASIZE, 0);
			if (ret <= 0) { 
				FD_CLR(sock_fd, &fdsr);
				break;
			} else {		// receive data
				buf[ret] = '\0';
				DecodeXml(buf);
			}
		}
	}
	printf("ERROR:CMSServer is down, please restart it ...\n");

#if	(__VXWORKS_OS__ || __LINUX_OS__)
	close(sock_fd); 
#elif __WIN32_OS__
	closesocket(sock_fd);
	WSACleanup();
#endif
}


int DecodeXml(char * buffer){
	TiXmlDocument *handle = new TiXmlDocument();
	handle->Parse(buffer);
	TiXmlNode* EnvelopeNode = handle->FirstChild("Envelope");
	const char * EnvelopeType = EnvelopeNode->ToElement()->Attribute("type");
	if(EnvelopeType != NULL){
		if(!strcmp(EnvelopeType,"r_vregister")){
			cms_fd = sock_fd;
			cout << "INFO:Videoserver registered to a CMSServer...\n"<<endl;
		}
		if(cms_fd != sock_fd){
			cout << "WARNING:got a unregistered message..." <<endl;
		}else{
			if(!strcmp(EnvelopeType, "startdeal")){
				//const char * Category = EnvelopeNode->ToElement()->Attribute("category");
				struct vsession * vs = new vsession;
				memset(vs,0,sizeof(struct vsession));
				strcpy(vs->warning_type,EnvelopeNode->ToElement()->Attribute("category"));
				strcpy(vs->mac , EnvelopeNode->FirstChildElement("mac")->GetText());
				strcpy(vs->cfd , EnvelopeNode->FirstChildElement("cfd")->GetText());
				strcpy(vs->rtspurl , EnvelopeNode->FirstChildElement("rtspuri")->GetText());
				TiXmlNode* AlarmNode = EnvelopeNode->FirstChild("alarmline");
				if( AlarmNode != NULL){
					vs->startx = (int )atof(AlarmNode->FirstChildElement("startcol")->GetText());
					vs->starty = (int )atof(AlarmNode->FirstChildElement("startrow")->GetText());
					vs->endx = (int )atof(AlarmNode->FirstChildElement("endcol")->GetText());
					vs->endy = (int )atof(AlarmNode->FirstChildElement("endrow")->GetText());
				}
				printf("%s\n",vs->warning_type);
				if(strcmp(vs->warning_type,"0000000000000000") !=0 ){
					if(!videoSession->Lookup(vs->rtspurl)){
						cout <<"INFO:Start deal \""<<vs->rtspurl <<"\""<<endl;
						VideoUnderstanding *lw = new  VideoUnderstanding(vs);
						lw->run();
					}else{
						VideoUnderstanding *lwt = (VideoUnderstanding *)videoSession->Lookup(vs->rtspurl);
						lwt->stop();
						videoSession->Remove(vs->rtspurl);
						delete lwt; 
						cout <<"INFO:ReStart deal \""<<vs->rtspurl <<"\""<<endl;
						VideoUnderstanding *lw = new  VideoUnderstanding(vs);
						lw->run();
					}

				}else{
					if(videoSession->Lookup(vs->rtspurl)){
						cout <<"INFO:Stop deal \""<<vs->rtspurl <<"\""<<endl;
						VideoUnderstanding *lwt = (VideoUnderstanding *)videoSession->Lookup(vs->rtspurl);
						lwt->stop();
						videoSession->Remove(vs->rtspurl);
						lwt->sendstartreply("success");
						delete lwt; 
					}else{
						cout<<"INFO: \""<<vs->rtspurl<<"\" didn't exsit"<<endl;
						VideoUnderstanding *lw = new  VideoUnderstanding(vs);
						lw->sendstartreply("fail");
						delete lw;
					}
				}

			}
		}
	}
	delete handle;
	return 0;
}