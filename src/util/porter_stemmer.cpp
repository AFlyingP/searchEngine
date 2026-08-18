#include "util/porter_stemmer.hpp"

#include <string>

namespace needlefish {

namespace {

class PorterContext {
  public:
    explicit PorterContext(std::string_view word)
        : b_(word), k_(static_cast<int>(word.size()) - 1), k0_(0), j_(0) {}

    std::string run() {
        if (k_ <= k0_ + 1) {
            return b_;
        }

        step1ab();
        if (k_ > k0_) {
            step1c();
        }
        if (k_ > k0_) {
            step2();
        }
        if (k_ > k0_) {
            step3();
        }
        if (k_ > k0_) {
            step4();
        }
        if (k_ > k0_) {
            step5();
        }

        b_.resize(static_cast<size_t>(k_ + 1));
        return b_;
    }

  private:
    [[nodiscard]] bool cons(int i) const noexcept {
        const char ch = b_[static_cast<size_t>(i)];
        if (ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u') {
            return false;
        }
        if (ch == 'y') {
            return (i == k0_) ? true : !cons(i - 1);
        }
        return true;
    }

    [[nodiscard]] int m() const noexcept {
        int n = 0;
        int i = k0_;
        while (true) {
            if (i > j_) {
                return n;
            }
            if (!cons(i)) {
                break;
            }
            i++;
        }
        i++;
        while (true) {
            while (true) {
                if (i > j_) {
                    return n;
                }
                if (cons(i)) {
                    break;
                }
                i++;
            }
            i++;
            n++;
            while (true) {
                if (i > j_) {
                    return n;
                }
                if (!cons(i)) {
                    break;
                }
                i++;
            }
            i++;
        }
    }

    [[nodiscard]] bool vowelinstem() const noexcept {
        for (int i = k0_; i <= j_; ++i) {
            if (!cons(i)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool doublec(int i) const noexcept {
        if (i < k0_ + 1) {
            return false;
        }
        if (b_[static_cast<size_t>(i)] != b_[static_cast<size_t>(i - 1)]) {
            return false;
        }
        return cons(i);
    }

    [[nodiscard]] bool cvc(int i) const noexcept {
        if (i < k0_ + 2 || !cons(i) || cons(i - 1) || !cons(i - 2)) {
            return false;
        }
        const char ch = b_[static_cast<size_t>(i)];
        if (ch == 'w' || ch == 'x' || ch == 'y') {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ends(std::string_view s) noexcept {
        const int len = static_cast<int>(s.size());
        if (len > k_ - k0_ + 1) {
            return false;
        }
        if (b_.compare(static_cast<size_t>(k_ - len + 1), s.size(), s) != 0) {
            return false;
        }
        j_ = k_ - len;
        return true;
    }

    void setto(std::string_view s) {
        const int len = static_cast<int>(s.size());
        b_.replace(static_cast<size_t>(j_ + 1), static_cast<size_t>(k_ - j_), s);
        k_ = j_ + len;
    }

    void r(std::string_view s) {
        if (m() > 0) {
            setto(s);
        }
    }

    void step1ab() {
        if (k_ <= k0_)
            return;
        if (b_[static_cast<size_t>(k_)] == 's') {
            if (ends("sses")) {
                k_ -= 2;
            } else if (ends("ies")) {
                setto("i");
            } else if (k_ > k0_ && b_[static_cast<size_t>(k_ - 1)] != 's') {
                k_--;
            }
        }
        if (ends("eed")) {
            if (m() > 0) {
                k_--;
            }
        } else if ((ends("ed") || ends("ing")) && vowelinstem()) {
            k_ = j_;
            if (ends("at")) {
                setto("ate");
            } else if (ends("bl")) {
                setto("ble");
            } else if (ends("iz")) {
                setto("ize");
            } else if (doublec(k_)) {
                k_--;
                const char ch = b_[static_cast<size_t>(k_)];
                if (ch == 'l' || ch == 's' || ch == 'z') {
                    k_++;
                }
            } else if (m() == 1 && cvc(k_)) {
                setto("e");
            }
        }
    }

    void step1c() {
        if (k_ <= k0_)
            return;
        if (ends("y") && vowelinstem()) {
            b_[static_cast<size_t>(k_)] = 'i';
        }
    }

    void step2() {
        if (k_ <= k0_ + 1)
            return;
        switch (b_[static_cast<size_t>(k_ - 1)]) {
            case 'a':
                if (ends("ational")) {
                    r("ate");
                    break;
                }
                if (ends("tional")) {
                    r("tion");
                    break;
                }
                break;
            case 'c':
                if (ends("enci")) {
                    r("ence");
                    break;
                }
                if (ends("anci")) {
                    r("ance");
                    break;
                }
                break;
            case 'e':
                if (ends("izer")) {
                    r("ize");
                    break;
                }
                break;
            case 'l':
                if (ends("bli")) {
                    r("ble");
                    break;
                }
                if (ends("alli")) {
                    r("al");
                    break;
                }
                if (ends("entli")) {
                    r("ent");
                    break;
                }
                if (ends("eli")) {
                    r("e");
                    break;
                }
                if (ends("ousli")) {
                    r("ous");
                    break;
                }
                break;
            case 'o':
                if (ends("ization")) {
                    r("ize");
                    break;
                }
                if (ends("ation")) {
                    r("ate");
                    break;
                }
                if (ends("ator")) {
                    r("ate");
                    break;
                }
                break;
            case 's':
                if (ends("alism")) {
                    r("al");
                    break;
                }
                if (ends("iveness")) {
                    r("ive");
                    break;
                }
                if (ends("fulness")) {
                    r("ful");
                    break;
                }
                if (ends("ousness")) {
                    r("ous");
                    break;
                }
                break;
            case 't':
                if (ends("aliti")) {
                    r("al");
                    break;
                }
                if (ends("iviti")) {
                    r("ive");
                    break;
                }
                if (ends("biliti")) {
                    r("ble");
                    break;
                }
                break;
            case 'g':
                if (ends("logi")) {
                    r("log");
                    break;
                }
                break;
            default:
                break;
        }
    }

    void step3() {
        if (k_ <= k0_)
            return;
        switch (b_[static_cast<size_t>(k_)]) {
            case 'e':
                if (ends("icate")) {
                    r("ic");
                    break;
                }
                if (ends("ative")) {
                    r("");
                    break;
                }
                if (ends("alize")) {
                    r("al");
                    break;
                }
                break;
            case 'i':
                if (ends("iciti")) {
                    r("ic");
                    break;
                }
                break;
            case 'l':
                if (ends("ical")) {
                    r("ic");
                    break;
                }
                if (ends("ful")) {
                    r("");
                    break;
                }
                break;
            case 's':
                if (ends("ness")) {
                    r("");
                    break;
                }
                break;
            default:
                break;
        }
    }

    void step4() {
        if (k_ <= k0_ + 1)
            return;
        switch (b_[static_cast<size_t>(k_ - 1)]) {
            case 'a':
                if (ends("al"))
                    break;
                return;
            case 'c':
                if (ends("ance") || ends("ence"))
                    break;
                return;
            case 'e':
                if (ends("er"))
                    break;
                return;
            case 'i':
                if (ends("ic"))
                    break;
                return;
            case 'l':
                if (ends("able") || ends("ible"))
                    break;
                return;
            case 'n':
                if (ends("ant") || ends("ement") || ends("ment") || ends("ent"))
                    break;
                return;
            case 'o':
                if (ends("ion") && j_ >= k0_ &&
                    (b_[static_cast<size_t>(j_)] == 's' || b_[static_cast<size_t>(j_)] == 't'))
                    break;
                if (ends("ou"))
                    break;
                return;
            case 's':
                if (ends("ism"))
                    break;
                return;
            case 't':
                if (ends("ate") || ends("iti"))
                    break;
                return;
            case 'u':
                if (ends("ous"))
                    break;
                return;
            case 'v':
                if (ends("ive"))
                    break;
                return;
            case 'z':
                if (ends("ize"))
                    break;
                return;
            default:
                return;
        }
        if (m() > 1) {
            k_ = j_;
        }
    }

    void step5() {
        if (k_ <= k0_)
            return;
        j_ = k_;
        if (b_[static_cast<size_t>(k_)] == 'e') {
            const int a = m();
            if (a > 1 || (a == 1 && !cvc(k_ - 1))) {
                k_--;
            }
        }
        if (k_ > k0_ && b_[static_cast<size_t>(k_)] == 'l' && doublec(k_) && m() > 1) {
            k_--;
        }
    }

    std::string b_;
    int k_{0};
    int k0_{0};
    int j_{0};
};

}  // namespace

std::string PorterStemmer::stem(std::string_view word) {
    if (word.size() <= 2) {
        return std::string(word);
    }
    PorterContext ctx(word);
    return ctx.run();
}

}  // namespace needlefish
