## ThreadPool

C++ 实现 Fix | Cache 模式线程池

### 主要技术点

#### 如何设计 Cache 模式
cached 模式需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来，创建新线程的两个条件
1. 任务数量对于空闲线程，即`taskSize_ > idleThreadSize_`
2. 当前线程数量小于上限阈值，即`curThreadSize_ < threadSizeThreshHold_`

创建细节步骤：
1. 创建新的线程对象
2. 启动线程
3. 修改线程个数相关的变量

#### 如何设计 Result 接收返回值

- setVal()

- get()

### 难点

#### ThreadPool 析构时死锁问题

如何调试？
- Windows VS 调试线程调用堆栈
- Linux gdb 调试多线程, attach 调用堆栈


#### MacOS mutex 问题



