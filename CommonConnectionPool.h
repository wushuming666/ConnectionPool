#pragma once
/*
ʵ�����ӳ�ģ��
*/

#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <functional>
#include <condition_variable>
using namespace std;
#include "Connection.h"

//�̰߳�ȫ�ĵ���ģʽ
class ConnectionPool
{
public:
	//��ȡ���ӳض���ʵ��
	static ConnectionPool* getConnectionPool();
	//���ⲿ�ṩ�ӿ�, �����ӳ��л�ȡһ�����õĿ�������
	//������ָ������ⲿ���ͷŹ���
	shared_ptr<Connection> getConnection();
private:
	//����#1 ���캯��˽�л�
	ConnectionPool();
	//�������ļ��м���������
	bool loadConfigFile();
	//�����ڶ������߳���,ר�Ÿ�������������
	void produceConnectionTask();
	//ɨ�賬��maxIdleTimeʱ��Ŀ�������, ���ж�Ӧ�����ӻ���
	void scannerConnectionTask();

	string _ip;				//mysql��ip��ַ
	unsigned short _port;	//mysql�Ķ˿ں� 3306
	string _username;		//mysql��¼�û���
	string _dbname;			//mysql������
	string _passward;		//��¼����
	int _initSize;			//���ӳصĳ�ʼ������
	int _maxSize;			//���ӳص����������
	int _maxIdleTime;		//���ӳ�������ʱ��
	int _connectionTimeOut; //���ӳػ�ȡ���ӵĳ�ʱʱ��

	queue<Connection*> _connectionQue; //�洢mysql���ӵĶ���
	mutex _queueMutex;		//ά�����Ӷ��е��̻߳��ⰲȫ��
	atomic_int _connectionCnt; //��¼��������connection���ӵ�������
	condition_variable cv;	//������������, ���������������̺߳��������̵߳�ͨ��
};