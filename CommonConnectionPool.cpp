#include "CommonConnectionPool.h"
#include "public.h"
#include <iostream>

//线程安全的懒汉单例函数接口
ConnectionPool* ConnectionPool::getConnectionPool()
{
	static ConnectionPool pool; //lock和unlock
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
		if (idx == -1) { //无效的配置项目
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

//连接池的构造
ConnectionPool::ConnectionPool()
{
	//加载配置项
	if (!loadConfigFile())
	{
		return;
	}

	//创建初始数量的连接
	for (int i = 0; i < _initSize; i++)
	{
		Connection* p = new Connection();
		p->connect(_ip, _port, _username, _passward, _dbname);
		p->refreshAliveTime(); //刷新下开始空闲的起始时间
		_connectionQue.push(p);
		_connectionCnt++;
	}

	//启动一个新的线程, 作为连接的生产者 linux thread => pthread_create
	thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
	//守护线程
	produce.detach();

	//启动一个新的定时线程, 扫描超过maxIdleTime时间的空闲连接, 进行对应的连接回收
	thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
	scanner.detach();
}

//运行在独立的线程中,专门负责生产新连接
void ConnectionPool::produceConnectionTask()
{
	for (;;)
	{
		unique_lock<mutex> lock(_queueMutex);
		while (!_connectionQue.empty())
		{
			cv.wait(lock);  //队列不空, 此处生产线程进入等待状态
			//当调用对象的wait()方法时会释放获取的该对象的锁。
			//消费者消费完了告知生产者没有连接了,去生产
			//生产者发现确实没有了, 进行下一步  死循环里面套wait():防止假唤醒
			/*
			wait接口会将线程放在某个等待队列上并阻塞该线程直到某个事件到来，
			而notify_*接口则是发布事件，当内核监测到相应的事件后便会去唤醒
			相应等待队列上的等待线程，此时cv.wait内部会将mutex重新lock。
			*/
		}

		//连接数量没有到达上限, 继续创建新的连接
		if (_connectionCnt < _maxSize)
		{
			Connection* p = new Connection();
			p->connect(_ip, _port, _username, _passward, _dbname);
			p->refreshAliveTime();
			_connectionQue.push(p);
			_connectionCnt++;
		}

		//通知消费者线程, 可以消费连接了
		cv.notify_all();
		//唤醒其他等待获取对象锁的线程
		//出右括号释放锁
	}
}

//给外部提供接口, 从连接池中获取一个可用的空闲连接
shared_ptr<Connection> ConnectionPool::getConnection()
{
	unique_lock<mutex> lock(_queueMutex);
	while (_connectionQue.empty())
	{
		//不要写sleep, sleep是直接睡这么长时间
		//wait_for: 时间内收到通知 或 超时
		if(cv_status::timeout == cv.wait_for(lock, chrono::milliseconds(_connectionTimeOut)))
		{
			if (_connectionQue.empty())
			{
				LOG("获取空闲连接超时了...获取连接失败!");
				return nullptr;
			}
		}
	}

	/*
	shared_ptr智能指针析构时会把connection资源直接delete调, 相当于
	调用connection的析构函数,connection就被close掉了
	这里需要自定义shared_ptr的释放资源方式, 把connection直接归还到queue中
	*/
	shared_ptr<Connection> sp(_connectionQue.front(),
		[&](Connection *pcon) {
			//要考虑队列的线程安全  这里的抢锁发生在出作用域析构,不是现在
			unique_lock<mutex> lock(_queueMutex);
			pcon->refreshAliveTime();
			_connectionQue.push(pcon);
		}
	);
	_connectionQue.pop();
	//谁消费了队列的最后一个connection, 谁负责通知一下生产者
	if (_connectionQue.empty())
	{
		cv.notify_all(); 
	}
	return sp;
}

//扫描超过maxIdleTime时间的空闲连接, 进行对应的连接回收
void ConnectionPool::scannerConnectionTask()
{
	for (;;)
	{
		//通过sleep模拟定时效果
		this_thread::sleep_for(chrono::seconds(_maxIdleTime));

		//扫描整个队列, 释放多余的连接
		unique_lock<mutex> lock(_queueMutex);
		while (_connectionCnt > _initSize)
		{
			Connection* p = _connectionQue.front();
			if (p->getAliveTime() >= _maxIdleTime * 1000)
			{
				_connectionQue.pop();
				_connectionCnt--;
				delete p;  //释放连接 调用~Connection()
			}
			else
			{
				break; //队头的连接没有超过_maxIdleTime, 其他的肯定没有
			}
		}
	}
}