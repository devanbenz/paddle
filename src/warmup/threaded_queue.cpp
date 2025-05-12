#include <condition_variable>
#include <iostream>
#include <optional>
#include <ostream>
#include <queue>
#include <thread>
#include <unistd.h>

std::mutex m;
std::condition_variable cv;
bool item_ready = false;

void producer(std::queue<std::optional<int> > &queue) {
    for (int i = 0; i < 10; i++) {
        {
            std::unique_lock<std::mutex> lock(m);
            std::cout << "producer: " << i << std::endl;
            queue.push(std::make_optional<int>(i));
            item_ready = true;
            lock.unlock();
            cv.notify_one();
        }
        sleep(1);
    } {
        std::unique_lock<std::mutex> lock(m);
        queue.emplace(std::nullopt);
        item_ready = true;
        lock.unlock();
        cv.notify_one();
    }
}

void consumer(std::queue<std::optional<int> > &queue) {
    while (true) {
        std::optional<int> item{};
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&queue] {return !queue.empty();});
            item = queue.front();
            queue.pop();
            item_ready = false;
        }

        if (item.has_value()) {
            std::cout << "consumer: " << item.value() << std::endl;
        } else {
            std::cout << "consumer: empty" << std::endl;
            break;
        }
    }
}

int main() {
    std::queue<std::optional<int> > queue{};
    std::thread producer_thread([&queue] {
        producer(queue);
    });
    std::thread consumer_thread([&queue] {
        consumer(queue);
    });
    std::cout << "start" << std::endl;

    producer_thread.join();
    consumer_thread.join();

    std::cout << "end" << std::endl;
    return 0;
}
