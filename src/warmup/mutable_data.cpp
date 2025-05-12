#include <iostream>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <unistd.h>

void process_data(std::map<std::string, int> &d, std::mutex &m) {
    std::unique_lock<std::mutex> lock(m);
    for (const auto& [key, value] : d) {
        std::cout << "{" << key << "} = {" << value << "}" << std::endl;
        sleep(1);
    }
}

int main() {
    std::mutex mu;
    std::map<std::string, int> m{};
    m.insert(std::pair<std::string, int>("a", 1));
    m.insert(std::pair<std::string, int>("b", 2));
    m.insert(std::pair<std::string, int>("c", 3));
    m.insert(std::pair<std::string, int>("d", 4));
    std::thread t1([&m, &mu] {
        process_data(m, mu);
    });

    sleep(1);

    mu.lock();
    m["a"] = 100;
    mu.unlock();

    t1.join();

    for (const auto& [key, value] : m) {
        std::cout << key << " = " << value;
    }
    return 0;
}