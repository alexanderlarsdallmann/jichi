// wire_virtual.cpp -- the same design as wire_vtable.c, with C++'s own
// machinery: a class with virtual functions instead of a hand-made table.
// The compiler builds the vtable, sets the pointer in every constructor, and
// checks every call's signature. What it cannot add: a refusal that is a
// value (the factory still returns 0), or C89 portability.
//
// Build and run (from this directory), C++98 so the comparison is like-for-like:
//   g++ -std=c++98 -pedantic -Wall -Wextra -Werror -o wire_virtual wire_virtual.cpp && ./wire_virtual
#include <cstdio>
#include <cstring>
#include <string>

class Wire {                                   // the interface
public:
    explicit Wire(const std::string &model) : model_(model), calls_(0) {}
    virtual ~Wire() {}                          // deleting through Wire* is safe
    virtual std::string build(const std::string &prompt) = 0;
    virtual const char *name() const = 0;
    int calls() const { return calls_; }
protected:
    std::string model_;
    int calls_;
};

class OpenAIWire : public Wire {
public:
    explicit OpenAIWire(const std::string &m) : Wire(m) {}
    std::string build(const std::string &prompt) {
        ++calls_;
        return "{\"model\":\"" + model_
             + "\",\"messages\":[{\"role\":\"user\",\"content\":\"" + prompt + "\"}]}";
    }
    const char *name() const { return "openai"; }
};

class AnthropicWire : public Wire {
public:
    explicit AnthropicWire(const std::string &m) : Wire(m) {}
    std::string build(const std::string &prompt) {
        ++calls_;
        return "{\"model\":\"" + model_
             + "\",\"max_tokens\":64,\"messages\":[{\"role\":\"user\",\"content\":\"" + prompt + "\"}]}";
    }
    const char *name() const { return "anthropic"; }
};

// The factory: still the one place that knows the kinds.
static Wire *wire_create(const char *kind, const std::string &model)
{
    if (std::strcmp(kind, "openai") == 0) return new OpenAIWire(model);
    if (std::strcmp(kind, "anthropic") == 0) return new AnthropicWire(model);
    return 0;
}

int main()
{
    const char *kinds[3] = { "openai", "anthropic", "gopher" };
    for (int i = 0; i < 3; ++i) {
        Wire *w = wire_create(kinds[i], "jlu/qwen3");
        if (w == 0) {
            std::printf("%-9s  refused by the factory: unknown kind\n", kinds[i]);
            continue;
        }
        std::printf("%-9s  %s\n", w->name(), w->build("hello").c_str());
        std::printf("%-9s  calls so far: %d\n", w->name(), w->calls());
        delete w;
    }
    return 0;
}
