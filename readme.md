# C++11 语言级别的简易数据库连接池
虽然量不大，但是做之前没想到能做完，总得来说此刻挺开心的。

# 难点记录
## 1. 生产者-消费者模型，调用wait()后生产者是通过哪一步重新获得锁的？
[参考链接](https://www.zhihu.com/question/511290840?utm_campaign=Sharon&utm_medium=social&utm_oi=1106163464443330560&utm_psn=1598427380342558720&utm_source=wechat_session&utm_content=group3_supplementQuestions)

![27a74172bbc3caf2b7fc365266def5a](https://user-images.githubusercontent.com/74699943/212797874-c5ca08f2-937a-4d72-9683-afe948c6008a.png)

## 2. 马上再跑一遍压力测试的话数据库就报错，不过等两分钟后又一切正常
场景是 4线程、没有线程池，每个线程向mysql插入2500条数据（一共连接10000次数据库），一切都好好的，但如果我马上再跑一遍压力测试的话数据库就报错，不过等两分钟后又一切正常。
我又做了点小实验、推测是数据库承受不住1万多次的同时登录

MySQL的用户同时连接数据库时有限制的，我想这也是池子控制最大量的一部分原因

# 一、关键技术点
> 使用**C++语言级别**的开发。

MySQL数据库编程、线程安全的懒汉单例模式、queue、C++11多线程编程、线程互斥、线程同步通信、生产者-消费者模型、基于CAS的原子整形、智能指针shared_ptr、lambda表达式等。

# 二、项目背景
为了提高MySQL数据库（基于C/S设计）的访问瓶颈，除了在服务端添加缓存服务器缓存常见的数据之外（例如redis），还可以增加连接池来提高SQL的访问效率，在高并发情况下，大量的**TCP 三次握手、MySQL Server连接认证、MySQL Server关闭连接回收资源和TCP四次挥手**所带来的消耗十分明显，连接池的主要功能就是优化这些性能损耗。

# 三、连接池功能点介绍
连接池一般包含了数据库连接所用的ip地址、port端口号、用户名和密码以及其它的性能参数，例如初
始连接量，最大连接量，最大空闲时间、连接超时时间等。
**初始连接量**：初始创建这么多数量的连接，当应用发起MySQL连接请求时直接从池中获取**一个**可用的连接，使用完后不断开连接，而是将connection再归还给连接池。
**最大连接量**：当并发访问多时，初始池可能不够用，如果总数没有达到最大连接量就创建新的连接。当连接使用完后放回池中。
**最大空闲时间**：连接队列的长度可能超出初始连接量，在指定的最大空闲时间内没有被使用的连接将被回收。
**连接超时时间**：当MySQL的并发请求量过大，连接池中的数量已经达到了最大值，没有空闲的连接可供使用。再超过“连接超时时间”仍无法获取连接的话，获取连接失败。

# 四、功能实现设计
`ConnectionPool.cpp`和`ConnectionPool.h`：连接池代码实现
`Connection.cpp`和`Connection.h`：数据库操作代码、增删改查代码实现

## ConnectionPool
### 4.1 线程安全的懒汉单例模式
将构造函数私有化
将获取池的方法定义为静态
```cpp
static ConnectionPool* getConnectionPool();
```
实现方式如下
```cpp
//线程安全的懒汉单例函数接口
ConnectionPool* ConnectionPool::getConnectionPool()
{
	static ConnectionPool pool; //lock和unlock
	return &pool;
}
```
**构造函数一定一定要实现！！！**

### 4.2 连接池的初始化配置
1. 创建初始连接
2. 启动一个新的线程, 作为连接的生产者
3. 启动一个新的线程, 作为连接的生产者
```cpp
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

	//启动一个新的线程, 扫描回收
	thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
	scanner.detach();
}
```

### 4.3 生产者线程
[生产者消费者模型](https://www.zhihu.com/question/511290840?utm_campaign=Sharon&utm_medium=social&utm_oi=1106163464443330560&utm_psn=1598427380342558720&utm_source=wechat_session&utm_content=group3_supplementQuestions)

生产者线程的工作如下：
1. 先抢到锁，如果队列不为空，就说明不需要我去生产，然后我将锁释放掉，接口会将线程放在某个等待队列上并阻塞该线程直到某个事件到来，当内核监测到notify_*发布的事件后便会去唤醒相应等待队列上的等待线程，此时cv.wait内部会将mutex重新lock
2. 如果连接数量没有达到上限，继续创建新的连接
3. 通知消费者可以消费了，唤醒获取对象锁的线程

注意点如下：
1. wait()既释放锁又加锁
2. notify_* 并没有释放锁，出右括号释放锁

生产者代码如下：
```cpp
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
```

### 4.4 消费者（给外部提供接口, 从连接池中获取一个可用的空闲连接）
1. 抢到锁
2. 只要队列为空，就等待`_connectionTimeOut`时间，如果超时了队列仍为空，说明获取空闲连接超时了，返回nullptr。将队列判空写为循环是有可能没超时被唤醒但又没抢到资源。
3. 最后一个消费完的通知生产者生产。

注意点：
	使用`shared_ptr`智能指针需要自定义析构函数。因为shared_ptr智能指针析构时会把connection资源直接delete掉，相当于关闭了连接。所以要将connection直接归还给queue。

代码如下：
```cpp
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
	shared_ptr智能指针析构时会把connection资源直接delete掉, 相当于
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
```

### 4.5 最大空闲时间 —— 扫描回收线程
因为是queue，先进先出，所以如果队头没有超时，其他的也没有。

代码如下：
```cpp
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
```

## Connection
单条连接有以下的要求：
ip、端口号、用户名、登录密码、数据库名称、更新操作、查询操作、刷新连接的起始点、返回存活的时间

数据库的操作直接用MySQL官方封装好的库即可。

# 五、压力测试
| 数据量 | 未使用连接池花费时间               | 使用连接池花费时间                 |
| ------ | ---------------------------------- | ---------------------------------- |
| 1000   | 单线程：5694ms    四线程：2018ms   | 单线程：2781ms    四线程：1235ms   |
| 5000   | 单线程：28692ms    四线程：8740ms  | 单线程：13355ms    四线程：5692ms  |
| 10000  | 单线程：57895ms    四线程：17330ms | 单线程：26741ms    四线程：10840ms |

# 六、整体代码
可以访问我的 github [数据库连接池](https://github.com/wushuming666/ConnectionPool)

# 七、C++调用MySQL
MySQL数据库编程直接采用oracle公司提供的MySQL C/C++客户端开发包，在VS上需要进行相
应的头文件和库文件的配置，如下：
1.右键项目 - C/C++ - 常规 - 附加包含目录，填写mysql.h头文件的路径
2.右键项目 - 链接器 - 常规 - 附加库目录，填写libmysql.lib的路径
3.右键项目 - 链接器 - 输入 - 附加依赖项，填写libmysql.lib库的名字
4.把libmysql.dll动态链接库（Linux下后缀名是.so库）放在工程目录下

今天按照这个步骤不行了，需要在VC++目录配置下
![image](https://github.com/wushuming666/ConnectionPool/assets/74699943/8f63ae1d-fa08-4413-b253-449cc96d803a)

MySQL数据库C++代码封装如下：
```cpp
#include <mysql.h>
#include <string>
using namespace std;
#include "public.h"
// 数据库操作类
class MySQL
{
public:
	// 初始化数据库连接
	MySQL()
	{
		_conn = mysql_init(nullptr);
	}
	// 释放数据库连接资源
	~MySQL()
	{
		if (_conn != nullptr)
			mysql_close(_conn);
	}
	// 连接数据库
	bool connect(string ip, unsigned short port, string user, string password,
		string dbname)
	{
		MYSQL* p = mysql_real_connect(_conn, ip.c_str(), user.c_str(),
			password.c_str(), dbname.c_str(), port, nullptr, 0);
		return p != nullptr;
	}
	// 更新操作 insert、delete、update
	bool update(string sql)
	{
		if (mysql_query(_conn, sql.c_str()))
		{
			LOG("更新失败:" + sql);
			return false;
		}
		return true;
	}
	// 查询操作 select
	MYSQL_RES* query(string sql)
	{
		if (mysql_query(_conn, sql.c_str()))
		{
			LOG("查询失败:" + sql);
			return nullptr;
		}
		return mysql_use_result(_conn);
	}
private:
	MYSQL* _conn; // 表示和MySQL Server的一条连接
};
```
