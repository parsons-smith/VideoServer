#include <iostream>
#include "MutexQueue.h"
using namespace std;

template <class T>
MutexQueue<T>::MutexQueue()
{
	mutex = sys_os_create_mutex();
	NODE<T>* p = new NODE<T>;
	if (NULL == p){
		cout << "ERROR:Failed to malloc the node." << endl;
	}
	p->data = NULL;
	p->next = NULL;
	front = p;
	rear = p;
}

template <class T>
MutexQueue<T>::~MutexQueue()
{
	while (!empty()){
		pop();
	}
	sys_os_mutex_leave(mutex);
}

template <class T>
void MutexQueue<T>::push(T e)
{
	sys_os_mutex_enter(mutex);
	NODE<T>* p = new NODE<T>;
	if (NULL == p){
		cout << "ERROR:Failed to malloc the node." << endl;
	}
	p->data = e;
	p->next = NULL;
	rear->next = p;
	rear = p;
	sys_os_mutex_leave(mutex);
}

template <class T>
T MutexQueue<T>::pop()
{
	T e;
	sys_os_mutex_enter(mutex);
	if (front == rear){
		sys_os_mutex_leave(mutex);
		return NULL;
	}else{
		NODE<T>* p = front->next;
		front->next = p->next;
		e = p->data;
		if (rear == p){
			rear = front;
		}
		delete p; p = NULL;
		sys_os_mutex_leave(mutex);
		return e;
	}
}

template <class T>
T MutexQueue<T>::front_element()
{
	sys_os_mutex_enter(mutex);
	if (front == rear){
		sys_os_mutex_leave(mutex);
		return NULL;
	}else{
		NODE<T>* p = front->next;
		sys_os_mutex_leave(mutex);
		return p->data;
	}
}

template <class T>
T MutexQueue<T>::back_element()
{
	sys_os_mutex_enter(mutex);
	if (front == rear){
		sys_os_mutex_leave(mutex);
		return NULL;
	}else{
		sys_os_mutex_leave(mutex);
		return rear->data;
	}
}

template <class T>
int MutexQueue<T>::size()
{
	sys_os_mutex_enter(mutex);
	int count(0);
	NODE<T>* p = front;
	while (p != rear){
		p = p->next;
		count++;
	}
	sys_os_mutex_leave(mutex);
	return count;
}

template <class T>
bool MutexQueue<T>::empty()
{
	sys_os_mutex_enter(mutex);
	if (front == rear){
		sys_os_mutex_leave(mutex);
		return true;
	}else{
		sys_os_mutex_leave(mutex);
		return false;
	}
}