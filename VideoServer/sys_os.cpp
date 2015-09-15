/***************************************************************************************/
#include "sys_os.h"


/***************************************************************************************/
void * sys_os_create_mutex()
{
	void * p_mutex = NULL;
	
#if	__WIN32_OS__

	p_mutex  = CreateMutex(NULL, FALSE, NULL);

#elif (__LINUX_OS__ || __VXWORKS_OS__)
	
	int ret;

	p_mutex = (sem_t *)XMALLOC(sizeof(sem_t));
	ret = sem_init((sem_t *)p_mutex,0,1);
	if (ret != 0)
	{
		XFREE(p_mutex);
		return NULL;
	}

#endif

	return p_mutex;
}

void * sys_os_create_sig()
{	
	void * p_sig = NULL;
	
#if	__WIN32_OS__

	p_sig = CreateEvent(NULL, FALSE, FALSE, NULL);

#elif (__LINUX_OS__ || __VXWORKS_OS__)

	int ret;

	p_sig = XMALLOC(sizeof(sem_t));
	ret = sem_init((sem_t *)p_sig,0,0);
	if (ret != 0)
	{
		XFREE(p_sig);
		return NULL;
	}

#endif

	return p_sig;
}

void sys_os_destroy_sig_mutx(void * ptr)
{
	if (ptr == NULL)
		return;

#if	__WIN32_OS__

	CloseHandle(ptr);

#elif (__LINUX_OS__ || __VXWORKS_OS__)

	sem_destroy((sem_t *)ptr);
	XFREE(ptr);

#endif
}

int sys_os_mutex_enter(void * p_sem)
{
	int ret;
	
	if (p_sem == NULL)
		return -1;

#if	(__VXWORKS_OS__ || __LINUX_OS__)
	ret = sem_wait((sem_t *)p_sem);
	if (ret != 0)
		return -1;
		
#elif __WIN32_OS__

	ret = WaitForSingleObject(p_sem,INFINITE);
	if (ret == WAIT_FAILED)
		return -1;
		
#endif

	return 0;
}

void sys_os_mutex_leave(void * p_sem)
{
	if (p_sem == NULL)
		return;

#if	(__VXWORKS_OS__ || __LINUX_OS__)

	sem_post((sem_t *)p_sem);
	
#elif __WIN32_OS__

	ReleaseMutex(p_sem);
	
#endif
}

int sys_os_sig_wait(void * p_sig)
{
	int ret;
		
	if (p_sig == NULL)
		return -1;

#if	(__VXWORKS_OS__ || __LINUX_OS__)

	ret = sem_wait((sem_t *)p_sig);
	if (ret != 0)
		return -1;
		
#elif __WIN32_OS__

	ret = WaitForSingleObject(p_sig,INFINITE);
	if (ret == WAIT_FAILED)
		return -1;
		
#endif

	return 0;
}

int sys_os_sig_wait_timeout(void * p_sig, unsigned int ms)
{
#if	(__VXWORKS_OS__ || __LINUX_OS__)

	int ret;
	struct timespec ts;
	struct timeval tt;

	if (p_sig == NULL) return -1;
	
	gettimeofday(&tt,NULL);

	tt.tv_sec = tt.tv_sec + ms / 1000;
	tt.tv_usec = tt.tv_usec + (ms % 1000) * 1000;
	tt.tv_sec += tt.tv_usec / (1000 * 1000);
	tt.tv_usec = tt.tv_usec % (1000 * 1000);
	
	ts.tv_sec = tt.tv_sec;
	ts.tv_nsec = tt.tv_usec * 1000;

	ret = sem_timedwait((sem_t *)p_sig, &ts);
	if (ret == -1 && errno == ETIMEDOUT)
		return -1;
	else
		return 0;

#elif __WIN32_OS__

	DWORD ret;

	if (p_sig == NULL) return -1;
	
	ret = WaitForSingleObject(p_sig, ms);
	if (ret == WAIT_FAILED)
		return -1;
	else if (ret == WAIT_TIMEOUT)
		return -1;
		
#endif

	return 0;
}

void sys_os_sig_sign(void * p_sig)
{
	if (p_sig == NULL)
		return;

#if	(__VXWORKS_OS__ || __LINUX_OS__)

	sem_post((sem_t *)p_sig);
	
#elif __WIN32_OS__

	SetEvent(p_sig);
	
#endif
}

pthread_t sys_os_create_thread(void * thread_func, void * argv)
{
	pthread_t tid = 0;

#if	(__VXWORKS_OS__ || __LINUX_OS__)

	int ret = pthread_create(&tid,NULL,(void *(*)(void *))thread_func,argv);
	if (ret != 0)
	{
		perror("sys_os_create_thread::pthread_create failed!!!");
		exit(EXIT_FAILURE);
	}

#elif __WIN32_OS__

	HANDLE hret = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread_func, argv, 0, &tid);
	if (hret == NULL || tid == 0)
	{
		DWORD err = GetLastError();
		printf("sys_os_create_thread::CreateThread hret=%u, tid=%u, err=%u\r\n", hret, tid, err);
	}

#endif

	return tid;
}

void sys_os_thread_exit(){

#if	(__VXWORKS_OS__ || __LINUX_OS__)
	pthread_exit(0);
#elif __WIN32_OS__
	
#endif
}

unsigned int sys_os_get_ms()
{
	unsigned int ms = 0;

#if __LINUX_OS__

	struct timeval tv;
	gettimeofday(&tv, NULL);
	ms = tv.tv_sec * 1000 + tv.tv_usec/1000;

#elif __WIN32_OS__

	ms = GetTickCount();

#endif

	return ms;
}


static FILE * pMemLogFile = NULL;
static void * logMemFileMutex = NULL;

void mem_log_quit()
{
	if (pMemLogFile)
	{
		fclose(pMemLogFile);
		pMemLogFile = NULL;
	}

	if (logMemFileMutex)
	{
		sys_os_destroy_sig_mutx(logMemFileMutex);
		logMemFileMutex = NULL;
	}
}

int _mem_log_print(char *fmt, va_list argptr)
{
	int slen = 0;

	if (pMemLogFile == NULL)
	{
		pMemLogFile = fopen("./mem_log.txt", "wb+");
		logMemFileMutex = sys_os_create_mutex();
		
		if (logMemFileMutex == NULL)
		{
			printf("log init sem_init failed[%s]\r\n",strerror(errno));
		}
	}

	if (pMemLogFile == NULL)
		return 0;

	sys_os_mutex_enter(logMemFileMutex);

	slen = vfprintf(pMemLogFile,fmt,argptr);
	fflush(pMemLogFile);

	sys_os_mutex_leave(logMemFileMutex);

	return slen;
}

int mem_log_print(char * fmt,...)
{
	int slen;	
	va_list argptr;

	va_start(argptr,fmt);
	
	slen = _mem_log_print(fmt,argptr);
	
	va_end(argptr);
	
	return slen;
}

void * xmalloc(size_t size, const char * pFileName, int line)
{
	void * ptr = malloc(size);
	// mem_log_print("+++0X%p, %u, file[%s],line[%d]\r\n", ptr, size, pFileName, line);
	return ptr;
}

void xfree(void * ptr, const char * pFileName, int line)
{
	// mem_log_print("---0X%p, file[%s],line[%d]\r\n", ptr, pFileName, line);
	free(ptr);
}



