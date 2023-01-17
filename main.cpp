#include <iostream>
using namespace std;
#include "Connection.h"
#include "CommonConnectionPool.h"

int main()
{
	Connection conn;
	conn.connect("127.0.0.1", 3306, "root", "123456", "chat");
	/* 经测试，数据库连接成功
	Connection conn;
	char sql[1024] = { 0 };
	sprintf(sql, "insert into user(name,age,sex) values('%s',%d,'%s')",
		"li si", 22, "male");
	conn.connect("127.0.0.1", 3306, "root", "123456", "chat");
	conn.update(sql);
	*/

	//ConnectionPool* cp = ConnectionPool::getConnectionPool();
	//cp->loadConfigFile();

	clock_t begin = clock();

	thread t1([]() {
		ConnectionPool* cp = ConnectionPool::getConnectionPool();
		shared_ptr<Connection>sp = cp->getConnection();
		for (int i = 0; i < 2500; i++)
		{
			Connection conn;
			char sql[1024] = { 0 };
			sprintf(sql, "insert into user(name,age,sex) values('%s',%d,'%s')",
				"li si", 22, "male");
			//conn.connect("127.0.0.1", 3306, "root", "123456", "chat");
			//conn.update(sql);
			sp->update(sql);
		}
		});
	thread t2([]() {
		ConnectionPool* cp = ConnectionPool::getConnectionPool();
		shared_ptr<Connection>sp = cp->getConnection();
		for (int i = 0; i < 2500; i++)
		{
			Connection conn;
			char sql[1024] = { 0 };
			sprintf(sql, "insert into user(name,age,sex) values('%s',%d,'%s')",
				"li si", 22, "male");
			//conn.connect("127.0.0.1", 3306, "root", "123456", "chat");
			//conn.update(sql);
			sp->update(sql);
		}
		});
	thread t3([]() {
		ConnectionPool* cp = ConnectionPool::getConnectionPool();
		shared_ptr<Connection>sp = cp->getConnection();
		for (int i = 0; i < 2500; i++)
		{
			Connection conn;
			char sql[1024] = { 0 };
			sprintf(sql, "insert into user(name,age,sex) values('%s',%d,'%s')",
				"li si", 22, "male");
			//conn.connect("127.0.0.1", 3306, "root", "123456", "chat");
			//conn.update(sql);
			sp->update(sql);
		}
		});
	thread t4([]() {
		ConnectionPool* cp = ConnectionPool::getConnectionPool();
		shared_ptr<Connection>sp = cp->getConnection();
		for (int i = 0; i < 2500; i++)
		{
			Connection conn;
			char sql[1024] = { 0 };
			sprintf(sql, "insert into user(name,age,sex) values('%s',%d,'%s')",
				"li si", 22, "male");
			//conn.connect("127.0.0.1", 3306, "root", "123456", "chat");
			//conn.update(sql);
			sp->update(sql);
		}
		});

	t1.join();
	t2.join();
	t3.join();
	t4.join();
#if 0
	for (int i = 0; i < 10000; i++)
	{
#if 0
		Connection conn;
		char sql[1024] = { 0 };
		sprintf(sql, "insert into user(name,age,sex) values('%s',%d,'%s')",
			"li si", 22, "male");
		conn.connect("127.0.0.1", 3306, "root", "123456", "chat");
		conn.update(sql);
#endif

#if 1
		shared_ptr<Connection>sp = cp->getConnection();
		char sql[1024] = { 0 };
		sprintf(sql, "insert into user(name,age,sex) values('%s',%d,'%s')",
			"li si", 22, "male");
		sp->update(sql);
#endif
	}
#endif

	clock_t end = clock();
	cout << end - begin << "ms" << endl;

	return 0;
}