# C++11 语言级别的简易数据库连接池
虽然量不大，但是做之前没想到能做完，总得来说此刻挺开心的。

# 难点记录
## 1. 生产者-消费者模型，调用wait()后生产者是通过哪一步重新获得锁的？
[参考链接](https://www.zhihu.com/question/511290840?utm_campaign=Sharon&utm_medium=social&utm_oi=1106163464443330560&utm_psn=1598427380342558720&utm_source=wechat_session&utm_content=group3_supplementQuestions)

![27a74172bbc3caf2b7fc365266def5a](https://user-images.githubusercontent.com/74699943/212797874-c5ca08f2-937a-4d72-9683-afe948c6008a.png)

## 2. 马上再跑一遍压力测试的话数据库就报错，不过等两分钟后又一切正常
场景是 4线程、没有线程池，每个线程向mysql插入2500条数据（一共连接10000次数据库），一切都好好的，但如果我马上再跑一遍压力测试的话数据库就报错，不过等两分钟后又一切正常。
我又做了点小实验、推测是数据库承受不住1万多次的同时登录
