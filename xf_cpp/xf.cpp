#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <any>
#include <fstream>
#include <cstdlib>
#include "json.hpp"
class Env;
using json = nlohmann::json;
using Any = std::any;
using List = std::vector<Any>;
using RegisteredFunc = std::function<Any(Env*  , const List&)>;
template<typename T> bool isinstance(const Any& value) {
    return std::any_cast<T>(&value) != nullptr;
};
std::unordered_map<std::string, RegisteredFunc> COMMANDS;
class ReturnVal : public std::exception {
private:
    Any value;
public:
    // Constructor to set the error message
    ReturnVal(const Any& msg) : value(msg) {}

    const Any val() const noexcept {
        return value;
    }
};
class Literal;
class Code;
class Program;
class ALink;
Any run_eval_once(const Any& node);
Any real_value(const Any& vl);
std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    auto end = s.end();
    while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(start, end);
}
List parse(const std::string& text, char sep = ' ') {
    List result;
    std::string buf;
    int depth = 0;
    char in_quote = '\0';
    bool escape = false;

    for (char c : text) {
        if (escape) {
            buf.push_back(c);
            escape = false;
            continue;
        }

        if (c == '\\') {
            escape = true;
            continue;
        }

        if (in_quote != '\0') {
            buf.push_back(c);
            if (c == in_quote) {
                in_quote = '\0';   
            }
            continue;
        }

        if (c == '\'' || c == '"') {
            buf.push_back(c);
            in_quote = c;
            continue;
        }

        if (c == '(' || c == '[' || c == '{') {
            depth++;
            buf.push_back(c);
            continue;
        }
        if (c == ')' || c == ']'|| c == '}') {
            depth--;
            buf.push_back(c);
            continue;
        }

        if (c == sep && depth == 0) {
            result.push_back((Any)trim(buf));
            buf.clear();
        } else {
            buf.push_back(c);
        }
    }

    if (!buf.empty()) {
        result.push_back((Any)trim(buf));
    }

    return result;
}

class ALink{
    private:
        int addr;
    public:
        Env* env;
        ALink(int addr_, Env* env_);

        inline int as_addr(){
            return (*this).addr;
        };
        Any as_literal() const;
        bool to_bool(const Any& a) {
            Any v = real_value(a);

            if (isinstance<bool>(v)) return std::any_cast<bool>(v);
            if (isinstance<int>(v)) return std::any_cast<int>(v) != 0;
            if (isinstance<float>(v)) return std::any_cast<float>(v) != 0.0f;
            if (isinstance<std::string>(v)) return !std::any_cast<std::string>(v).empty();

            return true;
        }
        ~ALink();

};

class Env{
    private: 
        std::unordered_map<std::string, ALink> vars;
    public:
        List data;
        std::vector<int> refs;
        std::vector<int> free;

        Any as_literal();


        ALink set_var(const std::string& name,Any value){
            if (!isinstance<ALink>(value)){
                vars.emplace(name, ALink(alloc(value), this));
            }else{
                ALink val_link = std::any_cast<ALink>(value);
                vars.emplace(name, ALink(val_link.as_addr(), val_link.env));
            };
            return vars.at(name);
        };
        inline ALink get_var(const std::string& name){
            return vars.at(name);
        };
        int alloc(Any val){
            int addr;
            if (!free.empty()) {
                addr = free.back();
                free.pop_back(); // sadly pop doesn't return the value
                data[addr] = val;
                refs[addr] = 0;
            } else {
                addr = data.size();
                data.push_back(val);
                refs.push_back(0);
            };
            return addr;
        };
        inline Any get_heap(int addr){return data[addr];};
        inline void set_heap(int addr, Any val) noexcept {data[addr] = val;};
        inline void inc(int addr) {refs[addr]++;};
        inline void dec(int addr){
            refs[addr]--;
            if (refs[addr] <= 0){
                data[addr].reset();
                free.push_back(addr);
            };
        };
};
ALink::ALink(int addr_, Env* env_) : addr(addr_), env(env_) {
            env->inc(addr);
        };
 Any ALink::as_literal() const{
    return (*this).env->get_heap((*this).addr);
};
ALink::~ALink() {
            if (env) env->dec(addr);
        }
class BaseNode {};

class Node: public BaseNode{
    public:
        Env* env;
        List args;
        Node(List args, Env* env): env(env), args(args){}
};

class Literal: public BaseNode{
    public:
        Env* env;
        Any val;
        Literal(Any val,Env* env): env(env), val(val){};
        inline Any run() noexcept {return val;};
};


class Program;

class Link: public Literal{
    public:
    Any run(){
        return ALink(env->alloc((*this).val), (*this).env);
    };
    Link(Any val,Env* env): Literal(val, env){};
};
class Code: public Node{
    using super = Node;
    public:
        std::string name;
        Code(std::string name, List args, Env * env):Node(args,env), name(name){};
        RegisteredFunc register_as_command(std::string as_what, RegisteredFunc fnc){
            COMMANDS.emplace(name, fnc);
            return fnc;
        };
        Any run(){
            List evaluated_args;
            for (auto& a : args)
                evaluated_args.push_back(run_eval_once(a));
            return COMMANDS.at(name)(env, evaluated_args);
        };
};


class Program{
    public:
        List values;
        Program(List values): values(values){};
        Any run(){
            Any value_return;
            for (Any cd: values){
                try{
                    std::any_cast<Code>(cd).run();
                } catch (const ReturnVal& r){
                    value_return = r.val();
                    break;
                };
            };
            return value_return;
        };
};


bool is_all_digits(const std::string& s) {
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}
Any convert_to_links(Any a, Env* e) {
    if (isinstance<ALink>(a)) {
        return a;  // already a link
    }
    else if (isinstance<std::string>(a)) {
        return ALink(e->alloc(a), e);
    }
    else if (isinstance<std::vector<Any>>(a)) {
        auto vec = std::any_cast<std::vector<Any>>(a);
        for (size_t i = 0; i < vec.size(); ++i) {
            vec[i] = convert_to_links(vec[i], e);
        }
        return ALink(e->alloc(vec), e);
    }
    else if (isinstance<std::map<std::string, Any>>(a)) {
        auto mp = std::any_cast<std::map<std::string, Any>>(a);
        std::map<std::string, Any> converted;
        for (auto& [k,v] : mp) {
            converted[k] = convert_to_links(v, e);
        }
        return ALink(e->alloc(converted), e);
    }
    else if (isinstance<std::unordered_map<std::string, Any>>(a)) {
        auto mp = std::any_cast<std::unordered_map<std::string, Any>>(a);
        std::unordered_map<std::string, Any> converted;
        for (auto& [k,v] : mp) {
            converted[k] = convert_to_links(v, e);
        }
        return ALink(e->alloc(converted), e);
    }
    return a;
}
Any real_value(const Any& vl) {
    if (isinstance<ALink>(vl)) {
        ALink& link = const_cast<ALink&>(std::any_cast<const ALink&>(vl));
        return link.as_literal();
    }
    else if (isinstance<Literal>(vl)) {
        const Literal& lit = std::any_cast<const Literal&>(vl);
        return lit.val;
    }
    else if (isinstance<Code>(vl)) {
        Code& code = const_cast<Code&>(std::any_cast<const Code&>(vl));
        return real_value(code.run());
    }
    else if (isinstance<ALink>(vl)) {
        ALink& code = const_cast<ALink&>(std::any_cast<const ALink&>(vl));
        return real_value(code.as_literal());
    }
    else if (isinstance<Program>(vl)) {
        Program& prog = const_cast<Program&>(std::any_cast<const Program&>(vl));
        return real_value(prog.run());
    }
    else if (isinstance<List>(vl)) {
        return vl;
    }
    else {
        return vl;
    }
}
Program ast1(const std::string& code, Env* e) {
    List program;

    for (const Any& j : parse(code, '\n')) {
        std::string line = std::any_cast<std::string>(j);
        line = trim(line);
        if (line.empty()) continue;

        size_t pos = line.find(':');
        std::string name = (pos != std::string::npos) ? trim(line.substr(0, pos)) : line;
        std::string arg_part = (pos != std::string::npos) ? line.substr(pos + 1) : "";

        List raw_args = parse(arg_part, ',');

        List processed_args;
        processed_args.reserve(raw_args.size());

        for (const Any& a : raw_args) {
            std::string arg = trim(std::any_cast<std::string>(a));
            if (arg.empty()) {continue;};

            if (arg.size() >= 3 && arg.compare(0, 2, "${") == 0 && arg.back() == '}') {
                // Sub-expression: ${...}
                std::string inner = arg.substr(2, arg.size() - 3);
                processed_args.push_back(ast1(inner, e));
            } else if (is_all_digits(arg)) {
                processed_args.push_back(Literal(std::atoi(arg.c_str()), e));
            } else if (arg == "true") {
                processed_args.push_back(Literal(true, e));
            } else if (arg == "false") {
                processed_args.push_back(Literal(false, e));
            } else if (arg == "null") {
                processed_args.push_back(Literal(NULL, e));
            } else if (!arg.empty() && arg.front() == '"' && arg.back() == '"') {
                processed_args.push_back(Link(arg.substr(1,arg.size() - 2), e));
            } else if (!arg.empty() && arg.front() == '(' && arg.back() == ')') {
                // Nested ( ... )
                std::string inner = arg.substr(1, arg.size() - 2);
                processed_args.push_back(Literal(ast1(inner, e), e));
            } else if (!arg.empty() && (arg.front() == '{' || arg.front() == '[')) {
                // JSON object or array
                try {
                    json jval = json::parse(arg);
                    processed_args.push_back(Link(convert_to_links(jval, e), e));
                } catch (...) {
                    processed_args.push_back(Literal(arg, e)); // fallback
                }
            } else {
                // Try float, otherwise string
                try {
                    processed_args.push_back(Literal(std::stof(arg), e));
                } catch (...) {
                    processed_args.push_back(Literal(arg, e));
                };
            };
        };

        program.push_back(Code(name, std::move(processed_args), e));
    }

    return Program(std::move(program));
}
Any deep_copy(const Any& a) {
    // Primitive types
    if (isinstance<int>(a)) return a;
    if (isinstance<float>(a)) return a;
    if (isinstance<bool>(a)) return a;
    if (isinstance<std::string>(a)) return a;

    // List
    if (isinstance<List>(a)) {
        List original = std::any_cast<List>(a);
        List copied;
        copied.reserve(original.size());

        for (const Any& item : original) {
            copied.push_back(deep_copy(item));
        }
        return copied;
    }

    // Literal
    if (isinstance<Literal>(a)) {
        Literal lit = std::any_cast<Literal>(a);
        return Literal(deep_copy(lit.val), lit.env);
    }

    // Link
    if (isinstance<Link>(a)) {
        Link lnk = std::any_cast<Link>(a);
        return Link(deep_copy(lnk.val), lnk.env);
    }

    // Code
    if (isinstance<Code>(a)) {
        Code code = std::any_cast<Code>(a);

        List new_args;
        new_args.reserve(code.args.size());

        for (const Any& arg : code.args) {
            new_args.push_back(deep_copy(arg));
        }

        return Code(code.name, new_args, code.env);
    }

    // Program
    if (isinstance<Program>(a)) {
        Program prog = std::any_cast<Program>(a);

        List new_values;
        new_values.reserve(prog.values.size());

        for (const Any& v : prog.values) {
            new_values.push_back(deep_copy(v));
        }

        return Program(new_values);
    }

    // ALink (do NOT deep copy heap)
    if (isinstance<ALink>(a)) {
        return a;
    }

    return a;
}

void func_prepare(Any& ast, const std::string& old, const Any& repl, Env* env) {

    // Program
    if (isinstance<Program>(ast)) {
        Program& prog = *const_cast<Program*>(&std::any_cast<const Program&>(ast));

        for (Any& v : prog.values) {
            func_prepare(v, old, repl, env);
        }
    }

    // Code
    else if (isinstance<Code>(ast)) {
        Code& code = *const_cast<Code*>(&std::any_cast<const Code&>(ast));

        code.env = env;

        for (Any& arg : code.args) {
            func_prepare(arg, old, repl, env);
        }
    }

    // Literal
    else if (isinstance<Literal>(ast)) {
        Literal& lit = *const_cast<Literal*>(&std::any_cast<const Literal&>(ast));

        lit.env = env;

        if (isinstance<std::string>(lit.val)) {
            std::string val = std::any_cast<std::string>(lit.val);

            if (val == old) {
                lit.val = repl;
            }
        }
    }

    // Link
    else if (isinstance<Link>(ast)) {
        Link& lnk = *const_cast<Link*>(&std::any_cast<const Link&>(ast));

        lnk.env = env;

        if (isinstance<std::string>(lnk.val)) {
            std::string val = std::any_cast<std::string>(lnk.val);

            if (val == old) {
                lnk.val = repl;
            }
        }
    }

    // List (safety case)
    else if (isinstance<List>(ast)) {
        List& lst = *const_cast<List*>(&std::any_cast<const List&>(ast));

        for (Any& item : lst) {
            func_prepare(item, old, repl, env);
        }
    }
}
Any run_eval_once(const Any& node) {
    if (isinstance<Literal>(node)) {
        return std::any_cast<Literal>(node).val;  // return the literal value
    }
    if (isinstance<Link>(node)) {
        Link l = std::any_cast<Link>(node);
        return ALink(l.env->alloc(l.val), l.env);  // return a link, not evaluating inner
    }
    if (isinstance<ALink>(node)) {
        return std::any_cast<ALink>(node).as_literal();  // resolve one level only
    }
    if (isinstance<Code>(node)) {
        Code c = std::any_cast<Code>(node);

        List evaluated_args;
        for (auto& a : c.args) {
            if (isinstance<Literal>(a)) {
                evaluated_args.push_back(std::any_cast<Literal>(a).val);
            } else if (isinstance<Link>(a)) {
                Link l = std::any_cast<Link>(a);
                evaluated_args.push_back(ALink(l.env->alloc(l.val), l.env));
            } else {
                evaluated_args.push_back(a);  // leave everything else as-is
            }
        }

        return COMMANDS.at(c.name)(c.env, evaluated_args);  // one eval of code
    }
    if (isinstance<Program>(node)) {
        Program p = std::any_cast<Program>(node);
        if (!p.values.empty()) {
            Any first = p.values[0];  // evaluate only first top-level statement
            return run_eval_once(first);
        }
        return nullptr;
    }

    return node;  // everything else returned as-is
}
Any call_func(Env* env, const List& args) {
    std::string name = std::any_cast<std::string>(
        real_value(args[0])
    );

    ALink func_link = env->get_var(name);
    Any body = func_link.as_literal();

    // Deep copy AST
    Any copied = deep_copy(body);

    // Substitute arguments
    for (size_t i = 1; i < args.size(); ++i) {
        std::string key = "&{" + std::to_string(i - 1) + "}";
        func_prepare(copied, key, args[i], env);
    }

    return std::any_cast<Program>(copied).run();
}
Any echo(Env* env, const List& args) {
    if (args.empty()) return nullptr;

    Any current = args.at(0);

    bool resolved = false;
    while (!resolved) {
        if (isinstance<ALink>(current)) {
            current = std::any_cast<ALink>(current).as_literal();
        } else {
            Any next = real_value(current);
            
            if (next.type() == current.type()) {
                resolved = true;
            } else {
                current = next;
            }
        }
    }

    if (isinstance<std::string>(current)) {
        std::cout << std::any_cast<std::string>(current) << std::endl;
    } else if (isinstance<int>(current)) {
        std::cout << std::any_cast<int>(current) << std::endl;
    } else if (isinstance<float>(current)) {
        std::cout << std::any_cast<float>(current) << std::endl;
    } else if (isinstance<bool>(current)) {
        std::cout << (std::any_cast<bool>(current) ? "true" : "false") << std::endl;
    } else {
        std::cout << "[Unknown Type: " << current.type().name() << "]" << std::endl;
    }

    return nullptr; 
}
Any def_func(Env* env, const List& args) {
    std::string name = std::any_cast<std::string>(
        real_value(args[0])
    );

    Any func_body = args[1];
    env->set_var(name, func_body);

    return nullptr;
}
Any io_input(Env* env, const List& args) {
    if (!args.empty()) {
        std::cout << std::any_cast<std::string>(
            real_value(args[0])
        );
    }

    std::string line;
    std::getline(std::cin, line);
    return line;
}
Any get_var_cmd(Env* env, const List& args) {
    std::string name = std::any_cast<std::string>(
        real_value(args[0])
    );

    return env->get_var(name);
}
Any set_var_cmd(Env* env, const List& args) {
    std::string name = std::any_cast<std::string>(
        real_value(args[0])
    );

    return env->set_var(name, args[1]);
}
Any if_cmd(Env* env, const List& args) {
    bool cond = std::any_cast<bool>(
        real_value(args[0])
    );

    if (cond) {
        return std::any_cast<Program>(args[1]).run();
    } 
    else if (args.size() == 3) {
        return std::any_cast<Program>(args[2]).run();
    }

    return nullptr;
}
Any return_cmd(Env* env, const List& args) {
    throw ReturnVal(args[0]);
}
Any while_cmd(Env* env, const List& args) {
    while (std::any_cast<bool>(
        std::any_cast<Program>(args[0]).run()
    )) {
        std::any_cast<Program>(args[1]).run();
    }
    return nullptr;
}
Any get_index_cmd(Env* env, const List& args) {
    auto container = std::any_cast<std::vector<Any>>(
        real_value(args[0])
    );

    int index = std::any_cast<int>(
        real_value(args[1])
    );

    return container.at(index);
}
Any set_index_cmd(Env* env, const List& args) {
    ALink link = std::any_cast<ALink>(args[0]);
    int addr = link.as_addr();

    auto vec = std::any_cast<std::vector<Any>>(
        env->get_heap(addr)
    );

    int index = std::any_cast<int>(
        real_value(args[1])
    );

    vec[index] = args[2];

    env->set_heap(addr, vec);
    return args[2];
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary); 
    if (!file) {
        throw std::runtime_error("Failed to open file");
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string buffer(size, '\0');
    if (!file.read(&buffer[0], size)) {
        throw std::runtime_error("Failed to read file");
    }

    return buffer;
}

int main(int argc, char* argv[]){

    Env e;

    COMMANDS["echo"] = echo;
    COMMANDS["def"] = def_func;
    COMMANDS["input"] = io_input;
    COMMANDS["call"] = call_func;
    COMMANDS["gt_ind"] = get_index_cmd;
    COMMANDS["st_ind"] = set_index_cmd;
    COMMANDS["get"] = get_var_cmd;
    COMMANDS["set"] = set_var_cmd;
    COMMANDS["if"] = if_cmd;
    COMMANDS["return"] = return_cmd;
    COMMANDS["while"] = while_cmd;
    
    std::string file_contents = readFile(argv[1]);
    Program p = ast1(file_contents, &e);
    p.run();

    return 0;
};
