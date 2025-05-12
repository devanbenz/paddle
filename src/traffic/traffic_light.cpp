#include <condition_variable>
#include <csignal>
#include <net.hpp>

enum SignalState {
    RED,
    YELLOW,
    GREEN,
};

enum Dir {
    NS,
    EW,
};

struct Operation {
    std::string state_ns;
    std::string state_ew;
};

struct Signal {
    SignalState state;
    int expires_g;
    int expires_y;
};

/// state machine
///  G1 -> Y1 -> R1 -> G2 -> Y2 -> R2

class SignalLogic {
public:
    SignalLogic(SignalState color_ns = RED, SignalState color_ew = GREEN, int curr_clock = 0, int ns_expire = 30, int ew_expire = 40) : curr_clock_(curr_clock) {
        ns_ = Signal {
        .state = color_ns,
        .expires_g = ns_expire,
        .expires_y = 5,
        };
        ew_ = Signal {
        .state = color_ew,
        .expires_g = ew_expire,
        .expires_y = 5,
        };
    }

    ~SignalLogic() = default;

    void ButtonPressed(Dir dir) {
        switch (dir) {
            case NS: {
                auto cur_light = GetNS();
                if (cur_light == "G") {
                    curr_clock_ += 15;
                }
                break;
            }
            case EW: {
                auto cur_light = GetEW();
                if (cur_light == "G") {
                    curr_clock_ += 15;
                }
                break;
            }
        }
    }

    void SetNS(SignalState color) {
        if (color == GREEN && (ew_.state == GREEN || ew_.state == YELLOW)) {
            return;
        }
        ns_.state = color;
    }

    void SetEW(SignalState color) {
        if (color == GREEN && (ns_.state == GREEN || ns_.state == YELLOW)) {
            return;
        }
        ew_.state = color;
    }

    Operation AdvanceClock(const int adv) {
        switch (ns_.state) {
            case RED:
                SetNS(GREEN);
                break;
            case YELLOW:
                curr_clock_ += adv;
                if (curr_clock_ >= ns_.expires_y) {
                    curr_clock_ = 0;
                    SetNS(RED);
                }
                break;
            case GREEN:
                curr_clock_ += adv;
                if (curr_clock_ >= ns_.expires_g) {
                    curr_clock_ = 0;
                    SetNS(YELLOW);
                }
                break;
        }
        switch (ew_.state) {
            case RED:
                SetEW(GREEN);
                break;
            case YELLOW:
                curr_clock_ += adv;
                if (curr_clock_ >= ew_.expires_y) {
                    curr_clock_ = 0;
                    SetEW(RED);
                }
                break;
            case GREEN:
                curr_clock_ += adv;
                if (curr_clock_ >= ew_.expires_g) {
                    curr_clock_ = 0;
                    SetEW(YELLOW);
                }
                break;
        }

        return {.state_ew = GetEW(), .state_ns = GetNS()};
    }

    [[nodiscard]] std::string GetNS() const {
        switch (ns_.state) {
            case RED:
                return "R";
            case YELLOW:
                return "Y";
            case GREEN:
                return "G";
        }
        return "";
    }

    [[nodiscard]] std::string GetEW() const {
        switch (ew_.state) {
            case RED:
                return "R";
            case YELLOW:
                return "Y";
            case GREEN:
                return "G";
        }
        return "";
    }

    void print() const {
        std::cout << "ns: " << GetNS() << " ew: " << GetEW() << std::endl;
    }
private:
    Signal ns_;
    Signal ew_;
    int curr_clock_;
};

void handler(int signal) {
    std::cout << "shutting down...";
    exit(0);
}

int main() {
    struct sigaction sigIntAction{};
    sigIntAction.sa_handler = handler;
    sigemptyset(&sigIntAction.sa_mask);
    sigIntAction.sa_flags = 0;
    sigaction(SIGINT, &sigIntAction, nullptr);

    std::atomic<bool> ns_btn_pressed(false);
    std::atomic<bool> ew_btn_pressed(false);

    auto traffic_signal_logic = SignalLogic();
    auto ns_out = UdpStream("localhost", 10000);
    auto ew_out = UdpStream("localhost", 30000);
    auto ns_in = UdpListener(20000);
    auto ew_in = UdpListener(40000);

    auto ewinit =  traffic_signal_logic.GetEW();
    auto nsinit = traffic_signal_logic.GetNS();
    ew_out.Send(ewinit);
    ns_out.Send(nsinit);

    std::thread main_thread([&ns_out, &ew_out, &traffic_signal_logic, &ns_btn_pressed, &ew_btn_pressed]() {
        while (true) {
            if (ns_btn_pressed) {
                traffic_signal_logic.ButtonPressed(NS);
                ns_btn_pressed.store(false);
            } else if (ew_btn_pressed) {
                traffic_signal_logic.ButtonPressed(EW);
                ew_btn_pressed.store(false);
            }

            auto op = traffic_signal_logic.AdvanceClock(1);
            ew_out.Send(op.state_ew);
            ns_out.Send(op.state_ns);
            sleep(1);
        }
    });

     std::thread ew_btn( [&ew_in, &ew_btn_pressed] {
         while (true) {
             auto pressed = ew_in.Recv();
             if (!pressed.empty()) {
                 ew_btn_pressed.store(true);
             }
         }
     });

     std::thread ns_btn( [&ns_in, &ns_btn_pressed] {
         while (true) {
             auto pressed = ns_in.Recv();
             if (!pressed.empty()) {
                 ns_btn_pressed.store(true);
             }
         }
     });

    main_thread.join();
     ew_btn.join();
     ns_btn.join();
}
