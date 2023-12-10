#include "threadpool.h"
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10;    // 单位：秒

// -------- Result 方法实现 --------
Result::Result(std::shared_ptr<Task> sp, bool isValid)
    : task_(sp)
    , isValid_(isValid)
{
    task_->setResult(this);
}
    
// setVal方法，获取任务执行完的返回值的
void Result::setVal(Any any) // Task->run() 执行之后才有
{
    this->any_ = std::move(any);
    sem_.post();
}

// 用户调用 get 方法获取 task 的返回值
Any Result::get()
{
    if (!isValid_) {
        return "";
    }
    sem_.wait();
    return std::move(any_); // 注意 Any 没有左值的拷贝构造，需要转为右值
}


// -------- Task 方法实现 --------
Task::Task()
    : result_(nullptr)
{}

void Task::exec()
{
    if (result_)
        result_->setVal(run()); // 这里发生多态调用
}

void Task::setResult(Result* res)
{
    result_ = res;
}


// -------- Thread 方法实现 --------
Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_ ++)
{
}

// 线程的创建中 start 和 submitTask 都是用户调用，不涉及多线程，因此不必原子类型
int Thread::generateId_ = 0;

Thread::~Thread()
{
}

// 启动线程
void Thread::start()
{
    // t 是线程对象，传入自定义的线程函数 func_，并且需要一个参数
    std::thread t(func_, threadId_);   
    t.detach(); // 设置分离线程，pthread_detach
}

int Thread::getId() const
{
    return threadId_;
}


// -------- ThreadPool 方法实现 --------

ThreadPool::ThreadPool()
    : initThreadSize_(10)
    , taskSize_(0)
    , curThreadSize_(0)
    , idleThreadSize_(0)
    , taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
    , threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
    , isPoolRunning_(false)
{
}

ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;

    //!NOTE: 这个顺序避免了
    notEmpty_.notify_all();

    // 等待线程池里面所有的线程返回，两种状态：阻塞 | 正在执行任务中
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    exitCond_.wait(lock, [&]() -> bool { return threads_.size() == 0; });
}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{   
    if (checkRunningState())
        return ;
    poolMode_ = mode;
}

// 设置task任务队列上线阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
    if (checkRunningState())
        return ;
    taskQueMaxThreshHold_ = threshhold;
}

// 设置线程池 cached 模式下线程阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
    if (checkRunningState())
        return ;
    if (poolMode_ == PoolMode::MODE_CACHE) {
        threadSizeThreshHold_ = threshhold;
    }
}

bool ThreadPool::checkRunningState() const
{
    return isPoolRunning_;
}

// 给线程池「提交任务」
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    
    // 线程的通信，等待任务队列有空余 wait, wait_for, wait_until
    /* while (taskQueue_.size() == taskQueMaxThreshHold_) {
        notFull_.wait(lock);
    } */
    /* notFull_.wait(lock, [&]() -> bool {
        return taskQueue_.size() < taskQueMaxThreshHold_;
    }); */

    // 用户提交任务，最长阻塞不能超过 1s，否则判断提交任务失败，返回
    if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {
        return taskQueue_.size() < (size_t)taskQueMaxThreshHold_;
    })) {

        // 表示 notFull_ 等待了 1s，条件仍然没有满足
        std::cerr << "task queue is full, submit task failed.\n";

        // return task->getResult();  // 不合理：线程执行完task，task对象就被析构掉了
        return Result(sp, false);
    }

    // 如果有空余，把任务放入任务队列
    taskQueue_.emplace(sp);
    taskSize_ ++;

    // 因为放入了新任务，任务队列肯定不空，在 notEmpty_ 上进行通知
    notEmpty_.notify_all();
    
    // cached 模式需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
    // 创建新线程的条件：#1 任务数量对于空闲线程; #2 当前线程数量小于上限阈值
    if (poolMode_ == PoolMode::MODE_CACHE && 
        taskSize_ > idleThreadSize_ &&
        curThreadSize_ < threadSizeThreshHold_)
    {
        std::cout << ">>> create new thread...\n";

        // #1 创建新的线程对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        // threads_.emplace_back(std::move(ptr));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        
        // #2 启动线程
        threads_[threadId]->start();
        
        // #3 修改线程个数相关的变量
        curThreadSize_ ++;
        idleThreadSize_ ++;
    }
    
    return Result(sp, true);
}


// 开启线程池
void ThreadPool::start(int initThreadSize)
{
    isPoolRunning_ = true;
    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    // 创建线程对象
    for (int i = 0; i < initThreadSize_; ++ i) 
    {
        // 创建 thread 线程对象的时候，把线程函数给到 thread 线程对象
        auto ptr = std::make_unique<Thread>(Thread(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1)));
        // threads_.emplace_back(std::move(ptr));
        threads_.emplace(ptr->getId(), std::move(ptr));
    }

    // 启动所有线程 
    for (int i = 0; i < initThreadSize_; ++ i) 
    {
        threads_[i]->start();   // 执行线程函数
        idleThreadSize_ ++;     // 记录空闲线程数量
    }
}


// 定义线程函数「消费任务」
void ThreadPool::threadFunc(int threadId)
{
    auto lastTime = std::chrono::high_resolution_clock().now();

    while (isPoolRunning_) 
    {
        std::shared_ptr<Task> task;
        {
            // 先获取锁【注意锁的粒度】
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            std::cout << "tid: " << std::this_thread::get_id() << "，尝试获取任务...\n";

            // cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s，应该把多余的线程
            // 结束回收掉（超过initThreadSize_数量的线程要进行回收）
            // 当前时间 - 上一次线程执行的时间 > 60s
           
            // 每一秒中返回一次，如何区分超时返回还是有任务执行返回
            while (taskQueue_.size() == 0)
            {
                if (poolMode_ == PoolMode::MODE_CACHE)
                {
                    // 条件变量，超时返回了
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);

                        if (dur.count() >= THREAD_MAX_IDLE_TIME &&
                            curThreadSize_ > initThreadSize_)
                        {
                            threads_.erase(threadId);
                            curThreadSize_ --;
                            idleThreadSize_ --;

                            std::cout << "threadId: " << threadId << ' ' << std::this_thread::get_id() << " exit!\n";
                            return ;
                        }
                    }
                }
                else 
                {
                    // 只要任务队列里面没有任务就要等待 notEmpty_ 条件
                    notEmpty_.wait(lock);
                }

                // #1 线程池结束时回收被阻塞线程资源
                if (!isPoolRunning_) {
                    threads_.erase(threadId);
                    std::cout << "~threadId: " << threadId << ' ' << std::this_thread::get_id() << " exit!\n";
                    exitCond_.notify_all();
                    return ;
                }
            }
            
            idleThreadSize_ --;
            std::cout << "tid: " << std::this_thread::get_id() << ", 获取成功...\n";

            // 从任务队列中取一个任务出来
            task = taskQueue_.front();
            taskQueue_.pop();
            taskSize_ --;

            // #通知1：如果依然有剩余任务，可以继续通知其他线程执行任务
            if (taskQueue_.size() > 0) {
                notEmpty_.notify_all();
            }

            // #通知2：取出一个任务进行通知，可以继续提交生产任务
            notFull_.notify_all();
        }

        // 当前线程负责执行这个任务
        if (task != nullptr) {
            // task->run(); 
            task->exec(); // #1 执行任务；#2 把任务的返回值 setVal 方法给到 Result
        }
        idleThreadSize_ ++; // 任务执行完了

        // 更新线程执行完任务的时间
        lastTime = std::chrono::high_resolution_clock().now();
    }
    
    // #2 正在执行任务的线程被回收
    threads_.erase(threadId);
    std::cout << "~threadId: " << threadId << ' ' << std::this_thread::get_id() << " exit!\n";
    exitCond_.notify_all();
}

