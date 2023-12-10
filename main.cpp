#include "threadpool.h"

#include <iostream>
#include <chrono>
#include <thread>
using namespace std;

using ULL = unsigned long long;

class MyTask : public Task
{
public:
    MyTask(int begin, int end)
        : begin_(begin)
        , end_(end)
    {}
    // 问题一：怎么设计run函数的返回值，可以表示任意的类型
    // Java Python Object 是所有其它类类型的基类
    // C++17 Any类型
    Any run()  // run方法最终就在线程池分配的线程中去做执行了!
    {
        std::cout << "tid: " << std::this_thread::get_id() << " begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        ULL sum = 0;
        for (ULL i = begin_; i <= end_; i++)
            sum += i;
        std::cout << "tid: " << std::this_thread::get_id() << " end!" << std::endl;

        return sum;
    }

private:
    int begin_;
    int end_;
};

int main()
{
    ThreadPool pool;
    // pool.setMode(PoolMode::MODE_CACHE);
    pool.start(4);
#if 1
    // 测试 Result 机制
    Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
    Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
    Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
    
    pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
    pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
    pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

    // 随着task被执行完，task对象没了，依赖于task对象的Result对象也没了
    ULL sum1 = res1.get().cast_<ULL>();  // get返回了一个Any类型，怎么转成具体的类型呢？
    ULL sum2 = res2.get().cast_<ULL>();
    ULL sum3 = res3.get().cast_<ULL>();

    // Master - Slave线程模型
    // Master线程用来分解任务，然后给各个Slave线程分配任务
    // 等待各个Slave线程执行完任务，返回结果
    // Master线程合并各个任务结果，输出
    std::cout << (sum1 + sum2 + sum3) << std::endl;
#endif

#if 0 
    // 测试基本逻辑
    // pool.setTaskQueMaxThreshHold(3);

    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>()); // 提交失败
#endif

    getchar();

    return 0;
}