#include "CommonConnectionPool.h"
#include "public.h"
#include <iostream>

//�̰߳�ȫ���������������ӿ�
ConnectionPool* ConnectionPool::getConnectionPool()
{
	static ConnectionPool pool; //lock��unlock
	return &pool;
}

bool ConnectionPool::loadConfigFile()
{
	FILE* pf = fopen("mysql.ini", "r");
	if (pf == nullptr)
	{
		LOG("mysql.ini file is not exits!");
		return false;
	}

	while (!feof(pf))
	{
		char line[1024] = { 0 };
		fgets(line, 1024, pf);
		string str = line;
		int idx = str.find('=', 0);
		if (idx == -1) { //��Ч��������Ŀ
			continue;
		}
		//passward=123456\n
		int endidx = str.find('\n', idx);
		string key = str.substr(0, idx);
		string value = str.substr(idx + 1, endidx - idx - 1);
 		
		if (key == "ip") {
			_ip = value;
		}
		else if (key == "port") {
			_port = atoi(value.c_str());
		}
		else if (key == "username")
		{
			_username = value;
		}
		else if (key == "passward")
		{
			_passward = value;
		}
		else if (key == "initSize")
		{
			_initSize = atoi(value.c_str());
		}
		else if (key == "maxSize")
		{
			_maxSize = atoi(value.c_str());
		}
		else if (key == "maxIdleTime")
		{
			_maxIdleTime = atoi(value.c_str());
		}
		else if (key == "connectionTimeOut")
		{
			_connectionTimeOut = atoi(value.c_str());
		}
		else if (key == "dbname")
		{
			_dbname = value;
		}
	}

	return true;
}

//���ӳصĹ���
ConnectionPool::ConnectionPool()
{
	//����������
	if (!loadConfigFile())
	{
		return;
	}

	//������ʼ����������
	for (int i = 0; i < _initSize; i++)
	{
		Connection* p = new Connection();
		p->connect(_ip, _port, _username, _passward, _dbname);
		p->refreshAliveTime(); //ˢ���¿�ʼ���е���ʼʱ��
		_connectionQue.push(p);
		_connectionCnt++;
	}

	//����һ���µ��߳�, ��Ϊ���ӵ������� linux thread => pthread_create
	thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
	//�ػ��߳�
	produce.detach();

	//����һ���µĶ�ʱ�߳�, ɨ�賬��maxIdleTimeʱ��Ŀ�������, ���ж�Ӧ�����ӻ���
	thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
	scanner.detach();
}

//�����ڶ������߳���,ר�Ÿ�������������
void ConnectionPool::produceConnectionTask()
{
	for (;;)
	{
		unique_lock<mutex> lock(_queueMutex);
		while (!_connectionQue.empty())
		{
			cv.wait(lock);  //���в���, �˴������߳̽���ȴ�״̬
			//�����ö����wait()����ʱ���ͷŻ�ȡ�ĸö��������
			//�������������˸�֪������û��������,ȥ����
			//�����߷���ȷʵû����, ������һ��  ��ѭ��������wait():��ֹ�ٻ���
			/*
			wait�ӿڻὫ�̷߳���ĳ���ȴ������ϲ��������߳�ֱ��ĳ���¼�������
			��notify_*�ӿ����Ƿ����¼������ں˼�⵽��Ӧ���¼�����ȥ����
			��Ӧ�ȴ������ϵĵȴ��̣߳���ʱcv.wait�ڲ��Ὣmutex����lock��
			*/
		}

		//��������û�е�������, ���������µ�����
		if (_connectionCnt < _maxSize)
		{
			Connection* p = new Connection();
			p->connect(_ip, _port, _username, _passward, _dbname);
			p->refreshAliveTime();
			_connectionQue.push(p);
			_connectionCnt++;
		}

		//֪ͨ�������߳�, ��������������
		cv.notify_all();
		//���������ȴ���ȡ���������߳�
		//���������ͷ���
	}
}

//���ⲿ�ṩ�ӿ�, �����ӳ��л�ȡһ�����õĿ�������
shared_ptr<Connection> ConnectionPool::getConnection()
{
	unique_lock<mutex> lock(_queueMutex);
	while (_connectionQue.empty())
	{
		//��Ҫдsleep, sleep��ֱ��˯��ô��ʱ��
		//wait_for: ʱ�����յ�֪ͨ �� ��ʱ
		if(cv_status::timeout == cv.wait_for(lock, chrono::milliseconds(_connectionTimeOut)))
		{
			if (_connectionQue.empty())
			{
				LOG("��ȡ�������ӳ�ʱ��...��ȡ����ʧ��!");
				return nullptr;
			}
		}
	}

	/*
	shared_ptr����ָ������ʱ���connection��Դֱ��delete��, �൱��
	����connection����������,connection�ͱ�close����
	������Ҫ�Զ���shared_ptr���ͷ���Դ��ʽ, ��connectionֱ�ӹ黹��queue��
	*/
	shared_ptr<Connection> sp(_connectionQue.front(),
		[&](Connection *pcon) {
			//Ҫ���Ƕ��е��̰߳�ȫ  ��������������ڳ�����������,��������
			unique_lock<mutex> lock(_queueMutex);
			pcon->refreshAliveTime();
			_connectionQue.push(pcon);
		}
	);
	_connectionQue.pop();
	//˭�����˶��е����һ��connection, ˭����֪ͨһ��������
	if (_connectionQue.empty())
	{
		cv.notify_all(); 
	}
	return sp;
}

//ɨ�賬��maxIdleTimeʱ��Ŀ�������, ���ж�Ӧ�����ӻ���
void ConnectionPool::scannerConnectionTask()
{
	for (;;)
	{
		//ͨ��sleepģ�ⶨʱЧ��
		this_thread::sleep_for(chrono::seconds(_maxIdleTime));

		//ɨ����������, �ͷŶ��������
		unique_lock<mutex> lock(_queueMutex);
		while (_connectionCnt > _initSize)
		{
			Connection* p = _connectionQue.front();
			if (p->getAliveTime() >= _maxIdleTime * 1000)
			{
				_connectionQue.pop();
				_connectionCnt--;
				delete p;  //�ͷ����� ����~Connection()
			}
			else
			{
				break; //��ͷ������û�г���_maxIdleTime, �����Ŀ϶�û��
			}
		}
	}
}