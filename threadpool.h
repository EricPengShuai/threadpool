#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>
#include <thread>


/**
 * @brief Any类型：可以接收任意数据的类型
 * 1. 需要接受任意类型参数：模板
 * 2. 需要一个类型指向其他任意类型：基类指针指向派生类对象
 */
class Any
{
public:
    Any() = default;
    ~Any() = default;

    // 因为成员变量 base_ 已经是 unique_ptr 了，因为禁止拷贝构造和赋值很合理
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;

    Any(Any&&) = default;
	Any& operator=(Any&&) = default;

    // 这个构造函数可以让 Any 类型接收任意其它的数据
    template<typename T>
    Any(T data) : base_(std::make_unique<Derive<T>>(data))
    {}

    // 可以把 Any 对象里面存储的data数据提取出来
    template<typename T>
    T cast_()
    {
        // 如何将 base_ 指针指向 Derive 对象的 data 提取出来
        // 基类指针 => 派生类指针 RTTI
        Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr) {
            throw "type is unmatch!";
        }
        
        return pd->data_;
    }

private:
    class Base
    {
    public:
        virtual ~Base() = default;
    };
    
    template<typename T>
    class Derive : public Base
    {
    public: 
        Derive(T data) : data_(data)
        {}
        
        T data_;
    };

private:
    std::unique_ptr<Base> base_; // 定义一个基类的指针
};


/**
 * @brief 信号量类 
 * 互斥锁 + 条件变量
 */
class Semaphore
{
public:
    Semaphore(int limit = 0)
        : resLimit_(limit)
        , isExit_(false)
    {}

    ~Semaphore() {
        isExit_ = true;
    }

    // 消耗一个信号量资源
    void wait()
    {
        if (isExit_) return ; // 解决 MingGW 或者 GCC 编译器资源不释放死锁问题

        std::unique_lock<std::mutex> lock(mtx_);
        // 等待信号量有资源，没有资源的话，会阻塞当前线程
        cond_.wait(lock, [&]() -> bool { return resLimit_ > 0; });
        resLimit_ -= 1; // 消耗
    }

    // 增加一个信号量资源
    void post()
    {
        if (isExit_) return ;

        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_ += 1; // 增加
        cond_.notify_all();
    }

private:
    std::atomic_bool isExit_;
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};


// Task 类型的前置声明
class Task;

/**
 * @brief Result返回值类
 * 实现接收提交到线程池的task任务执行完成后的返回值类型Result
 */
class Result
{
public:
    Result(std::shared_ptr<Task> sp, bool isValid = true);
    
    ~Result() = default;

    // 问题一：setVal方法，获取任务执行完的返回值的
    void setVal(Any any);

    // 问题二：get方法，用户调用这个方法获取task的返回值
    Any get();

private:
    Any any_;       // 存储任务的返回值
    Semaphore sem_; // 线程通信的信号量
    std::shared_ptr<Task> task_;    // 指向对应获取返回值的任务对象
    std::atomic_bool isValid_;      // 返回值是否有效
};


// 任务抽象基类
class Task 
{
public:
    Task();
    ~Task() = default;

    void exec(); // 为了封装 run 并且返回 Result
    void setResult(Result* res);

    // 用户需要自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
    virtual Any run() = 0;

private:
    Result* result_; // 不能 shared_ptr，Result 对象的生命周期长于 Task
};


// 线程池支持的模式
enum class PoolMode {
    MODE_FIXED,  // 固定数量的线程
    MODE_CACHED,  // 线程数量可动态增长
};


// 线程类型
class Thread 
{
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void(int)>;

    Thread(ThreadFunc func);

    ~Thread();

    // 启动线程
    void start();

    // 获取线程id
    int getId() const;

private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_;  // 保存自定义线程id
};


/**
 * @brief 线程池类，例子：
 * ThreadPool pool;
 * pool.start(4);
 * 
 * class MyTask : public Task
 * {
 *      public:
 *          void run() { // 自定义的线程代码... }
 * }
 * 
 * pool.submitTask(std::make_shared<MyTask>());
 */
class ThreadPool 
{
public:
    ThreadPool();
    ~ThreadPool();

    // 设置线程池的工作模式
    void setMode(PoolMode mode);

    // 设置task任务队列上线阈值
    void setTaskQueMaxThreshHold(int threshhold);

    // 设置线程池 cached 模式下线程阈值
    void setThreadSizeThreshHold(int threshhold);

    // 给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    // 开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    // 定义线程函数
    void threadFunc(int threadId);
    void threadFunc_v0(int threadId); // 线程池析构时不等待任务执行完直接退出

    // 检查pool的运行状态
    bool checkRunningState() const;

private:
    // std::vector<std::unique_ptr<Thread>> threads_;  // 线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;

    int initThreadSize_;              // 初始的线程数量
    std::atomic_int curThreadSize_;   // 当前线程池里面线程的总数量
    std::atomic_int idleThreadSize_;  // 空闲线程数量
    int threadSizeThreshHold_;        // 线程数量上限阈值

    std::queue<std::shared_ptr<Task>> taskQueue_;  // 任务队列
    std::atomic_int taskSize_;                     // 任务的数量
    int taskQueMaxThreshHold_;                     // 任务队列数量上限阈值

    std::mutex taskQueMtx_;             // 保证任务队列的线程安全
    std::condition_variable notFull_;   // 表示任务队列不满
    std::condition_variable notEmpty_;  // 表示任务队列不空
    std::condition_variable exitCond_;  // 等待线程资源全部回收

    PoolMode poolMode_;  // 当前线程池的工作模式
    std::atomic_bool isPoolRunning_;
};

#endif
