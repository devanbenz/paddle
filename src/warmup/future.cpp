#include <future>
#include <iostream>
#include <unistd.h>

void f(int x, int y, std::promise<int>& promise) {
    sleep(5);
    promise.set_value(x+y);
}

int main() {
    std::promise<int> promise;
    std::thread t1([&promise] {
        f(10, 20, promise);
    });

    std::cout << promise.get_future().get() << std::endl;
    t1.join();
}