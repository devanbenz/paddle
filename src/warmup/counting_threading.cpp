#include <iostream>
#include <ostream>
#include <thread>
#include <unistd.h>

std::mutex m;

void countdown(int n) {
    while (n > 0) {
        m.lock();
        std::cout << "Down we go! " << n << std::endl;
        sleep(1);
        n -= 1;
        m.unlock();
    }
}

void countup(const int stop) {
    int n = 0;
    while (n < stop) {
        m.lock();
        std::cout << "Up we go! " << n << std::endl;
        sleep(1);
        n += 1;
        m.unlock();
    }
}

int main() {
    std::thread t1([] {
        countup(10);
    });
    std::thread t2([] {
        countdown(5);
    });

    std::cout << "Starting threads" << std::endl;

    t1.join();
    t2.join();

    std::cout << "Done" << std::endl;
    return 0;
}