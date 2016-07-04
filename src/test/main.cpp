#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <utility>
#include <vector>

using namespace std;

//------------------------------------------------------------------------------

namespace
{
  template <typename InputIt, typename OutputIt>
  OutputIt escape(InputIt first,
                  InputIt last,
                  OutputIt dest)
  {
    for (; first != last; ++first)
    {
      if (*first == '\n') {
        *dest++ = '\\';
        *dest++ = 'n';
      } else if (*first == '\\'
                 || *first == '\"') {
        *dest++ = '\\';
        *dest++ = *first;
      } else {
        *dest++ = *first;
      }
    }
    return dest;
  }

  template <typename InputIt, typename OutputIt>
  OutputIt unescape(InputIt first,
                    InputIt last,
                    OutputIt dest)
  {
    for (; first != last; ++first)
    {
      if (*first == '\\') {
        ++first;
        switch (*first) {
          case 'n': *dest++ = '\n'; break;
          default: *dest++ = *first; break;
        }
      } else {
        *dest++ = *first;
      }
    }
    return dest;
  }
}

//------------------------------------------------------------------------------

using Token = string;

vector<Token> tokenizer(const string& s)
{
  static const string tokenPattern =
    R"([[:space:],]*()" // open paren after separator
    R"(~@)"
    R"(|[][{}()~@^'`])" // other characters
    R"(|"(\\.|[^\"])*")" // string
    R"(|;.*)" // comment
    R"(|[^][[:space:]{}();,^'`\"]+)" // atom
    R"())"; // close paren

  static const regex re(tokenPattern, regex::extended);

  vector<Token> v;
  for (auto i = sregex_iterator(s.cbegin(), s.cend(), re);
       i != sregex_iterator();
       ++i)
  {
    v.push_back(i->str(1));
  }
  return v;
}

//------------------------------------------------------------------------------

class Reader
{
public:
  Reader(vector<Token>&& tokens) :m_tokens(std::move(tokens)) {}

  Token next() { return m_tokens[pos++]; }
  Token peek() const { return m_tokens[pos]; }
  bool empty() const { return pos >= m_tokens.size(); }

  vector<Token> m_tokens;
  size_t pos = 0;
};

//------------------------------------------------------------------------------

struct Form;
using FormPtr = shared_ptr<Form>;

class Environment
{
public:
  Environment(Environment* parent = nullptr)
    : m_parent(parent)
  {}

  FormPtr lookup(const string& s)
  {
    auto i = m_bindings.find(s);
    if (i == m_bindings.end()) {
      if (!m_parent) return nullptr;
      return m_parent->lookup(s);
    }
    return i->second;
  }

  void set(const string&s, const FormPtr& f)
  {
    m_bindings.emplace(s, f);
  }

  Environment* find(const string& s)
  {
    auto i = m_bindings.find(s);
    if (i == m_bindings.end()) {
      if (!m_parent) return nullptr;
      return m_parent->find(s);
    }
    return this;
  }

private:
  map<string, FormPtr> m_bindings;
  Environment* m_parent;
};

//------------------------------------------------------------------------------

struct Form : public enable_shared_from_this<Form>
{
  virtual ~Form() {}
  virtual FormPtr eval(Environment&) { return shared_from_this(); }
  virtual string print() const { return "<form>"; }
  virtual bool is_truthy() const { return true; }
  virtual FormPtr apply(Environment&) const { return FormPtr{}; };

  // convenience for checking symbol equality
  virtual bool symb_eq(const string&) { return false; }
};

struct Nil : public Form
{
  virtual string print() const { return "nil"; }
  virtual bool is_truthy() const { return false; }
};

struct True : public Form
{
  virtual string print() const { return "true"; }
};

struct False : public Form
{
  virtual string print() const { return "false"; }
  virtual bool is_truthy() const { return false; }
};

FormPtr eval_list(const vector<FormPtr>& v, Environment& e);

struct List : public Form
{
  List(vector<FormPtr>&& v) : m_elements(std::move(v)) {}

  virtual string print() const
  {
    string s;
    s.push_back('(');
    auto i = m_elements.cbegin();
    if (i != m_elements.cend()) {
      s += (*i)->print();
      ++i;
    }
    while (i != m_elements.cend()) {
      s.push_back(' ');
      s += (*i)->print();
      ++i;
    }
    s.push_back(')');
    return s;
  }

  virtual FormPtr eval(Environment& e)
  {
    return eval_list(m_elements, e);
  }

  vector<FormPtr> m_elements;
};

struct String : public Form
{
  String(const string& s)
  {
    unescape(s.cbegin()+1, s.cend()-1,
             back_inserter(m_value));
  }

  virtual string print() const
  {
    string s;
    s.push_back('"');
    escape(m_value.cbegin(), m_value.cend(),
           back_inserter(s));
    s.push_back('"');
    return s;
  }

  string m_value;
};

struct Number : public Form
{
  Number(const string& s)
  {
    m_value = stoi(s);
  }
  Number(int n) : m_value(n) {}

  virtual string print() const
  {
    return to_string(m_value);
  }

  int m_value;
};

struct Symbol : public Form
{
  Symbol(const string& s) : m_value(s) {}
  virtual string print() const { return m_value; }

  virtual FormPtr eval(Environment& e)
  {
    auto f = e.lookup(m_value);
    if (!f) {
      cout << "Unbound symbol: " << m_value << endl;
    }
    return f;
  }

  virtual bool symb_eq(const string& s) { return s == m_value; }

  string m_value;
};

struct Function : public Form
{
  Function(vector<string>&& params, const FormPtr& body)
    : m_params(std::move(params))
    , m_body(body)
  {}

  virtual string print() const { return "<function>"; }

  virtual FormPtr apply(Environment &e) const
  {
    return m_body->eval(e);
  }

  vector<string> m_params;
  FormPtr m_body;
};

struct BuiltinFunction : public Function
{
  BuiltinFunction(vector<string>&& params,
                  function<FormPtr(Environment&)>&& f)
    : Function(std::move(params), nullptr)
    , m_f(std::move(f))
  {}

  virtual string print() const { return "<builtin function>"; }

  virtual FormPtr apply(Environment &e) const
  {
    return m_f(e);
  }

  function<FormPtr(Environment&)> m_f;
};

//------------------------------------------------------------------------------

FormPtr read_form(Reader& r);

FormPtr read_list(Reader& r)
{
  vector<FormPtr> v;

  r.next(); // skip open paren
  if (r.empty()) {
    cout << "Error: unterminated read (list)" << endl;
    return nullptr;
  }

  while (r.peek()[0] != ')')
  {
    v.emplace_back(read_form(r));
    if (r.empty()) {
      cout << "Error: unterminated read (list)" << endl;
      return nullptr;
    }
  }
  r.next(); // eat close paren

  if (v.empty())
  {
    return make_shared<Nil>();
  }
  return make_shared<List>(std::move(v));
}

FormPtr read_atom(Reader& r)
{
  auto t = r.next();

  if (t[0] == '"') {
    return make_shared<String>(std::move(t));
  }
  if (isdigit(t[0])) {
    return make_shared<Number>(std::move(t));
  }
  if (t == "true") {
    return make_shared<True>();
  }
  if (t == "false") {
    return make_shared<False>();
  }
  if (t[0] == ';') {
    return nullptr;
  }
  return make_shared<Symbol>(std::move(t));
}

FormPtr read_form(Reader& r)
{
  if (r.empty()) return nullptr;

  auto t = r.peek();
  switch (t[0])
  {
    case '(':
      return read_list(r);
      break;
    default:
      return read_atom(r);
      break;
  }
}

auto read(const string& s)
{
  auto t = tokenizer(s);
  auto r = Reader(std::move(t));
  return read_form(r);
}

//------------------------------------------------------------------------------

auto eval(const FormPtr& form, Environment& e)
{
  if (form) return form->eval(e);
  return FormPtr{};
}

FormPtr eval_let(const vector<FormPtr>& v, Environment& e)
{
  if (v.size() != 3) {
    cout << "Wrong number of arguments to let, expecting 2, got "
         << v.size()-1 << endl;
    return nullptr;
  }

  List* l = dynamic_cast<List*>(v[1].get());
  if (!l) {
    cout << "First argument to let must be a list" << endl;
    return nullptr;
  }

  auto binding = l->m_elements;
  if (binding.size() != 2) {
    cout << "Too many elements in let binding list" << endl;
    return nullptr;
  }

  Environment let_env(e);
  let_env.set(binding[0]->print(), binding[1]->eval(e));

  return eval(v[2], let_env);
}

FormPtr eval_if(const vector<FormPtr>& v, Environment& e)
{
  if (v.size() != 4) {
    cout << "Wrong number of arguments to if, expecting 3, got "
         << v.size()-1 << endl;
    return nullptr;
  }

  auto f = eval(v[1], e);
  if (f->is_truthy())
  {
    return eval(v[2], e);
  }
  else
  {
    return eval(v[3], e);
  }
}

FormPtr eval_lambda(const vector<FormPtr>& v, Environment&)
{
  if (v.size() != 3) {
    cout << "Wrong number of arguments to lambda, expecting 2, got "
         << v.size()-1 << endl;
    return nullptr;
  }

  List* l = dynamic_cast<List*>(v[1].get());
  if (!l) {
    cout << "First argument to lambda must be a list" << endl;
    return nullptr;
  }

  // TODO: close over values in env (lexical scope)

  vector<string> params;
  for (const auto& f : l->m_elements)
  {
    params.emplace_back(f->print());
  }
  return make_shared<Function>(std::move(params), v[2]);
}

FormPtr apply(const Function& f,
              typename vector<FormPtr>::const_iterator first,
              typename vector<FormPtr>::const_iterator last,
              Environment& e)
{
  auto num_params = f.m_params.size();
  decltype(num_params) supplied_args = distance(first, last);
  if (num_params != supplied_args) {
    cout << "Not enough arguments to function, expecting "
         << f.m_params.size() << ", got " << distance(first, last) << endl;
    return nullptr;
  }

  Environment apply_env(e);
  for (auto i = f.m_params.cbegin(); first != last; ++i, ++first)
  {
    auto arg = (*first)->eval(e);
    if (!arg) {
      cout << "Could not evaluate function param: " << (*first)->print();
      return nullptr;
    }
    apply_env.set(*i, arg);
  }

  return f.apply(apply_env);
}

FormPtr eval_set(const vector<FormPtr>& v, Environment& e)
{
  if (v.size() != 3) {
    cout << "Wrong number of arguments to set!, expecting 2, got "
         << v.size()-1 << endl;
    return nullptr;
  }

  Symbol* s = dynamic_cast<Symbol*>(v[1].get());
  if (!s) {
    cout << "First argument to set! must be a symbol" << endl;
    return nullptr;
  }

  auto r = v[2]->eval(e);
  e.set(s->print(), r);
  return r;
}

FormPtr eval_list(const vector<FormPtr>& v, Environment& e)
{
  if (v.front()->symb_eq("let")) {
    return eval_let(v, e);
  }
  if (v.front()->symb_eq("if")) {
    return eval_if(v, e);
  }
  if (v.front()->symb_eq("lambda")) {
    return eval_lambda(v, e);
  }
  if (v.front()->symb_eq("set!")) {
    return eval_set(v, e);
  }

  auto form = v.front()->eval(e);
  Function *f = dynamic_cast<Function*>(form.get());
  if (f) {
    return apply(*f, v.cbegin()+1, v.cend(), e);
  }

  cout << "Don't know how to evaluate " << v.front()->print() << endl;
  return nullptr;
}

//------------------------------------------------------------------------------

void print(const FormPtr& form)
{
  if (!form) return;
  cout << form->print() << endl;
}

//------------------------------------------------------------------------------
template <typename F>
FormPtr builtin_numeric(Environment &e, const string& op, F&& f)
{
  FormPtr a = e.lookup("a");
  FormPtr b = e.lookup("b");
  auto anum = dynamic_cast<Number*>(a.get());
  auto bnum = dynamic_cast<Number*>(b.get());

  if (!a || !b) {
    cout << "Don't know how to " << op <<  " " << a->print()
         << " and " << b->print() << endl;
    return nullptr;
  }
  return make_shared<Number>(f(anum->m_value, bnum->m_value));
}

//------------------------------------------------------------------------------
template <typename F>
FormPtr builtin_divide(Environment &e, const string& op, F&& f)
{
  FormPtr a = e.lookup("a");
  FormPtr b = e.lookup("b");
  auto anum = dynamic_cast<Number*>(a.get());
  auto bnum = dynamic_cast<Number*>(b.get());

  if (!a || !b) {
    cout << "Don't know how to " << op <<  " " << a->print()
         << " and " << b->print() << endl;
    return nullptr;
  }

  if (bnum->m_value == 0) {
    cout << "Division by zero" << endl;
    return FormPtr{};
  }
  return make_shared<Number>(f(anum->m_value, bnum->m_value));
}

//------------------------------------------------------------------------------
static const char *prompt = "blisp> ";

unique_ptr<Environment> create_base_env()
{
  auto e = make_unique<Environment>();
  e->set("nil", make_shared<Nil>());

  e->set("+", make_shared<BuiltinFunction>(
             vector<string>{"a", "b"},
             [] (Environment&e) -> FormPtr {
               return builtin_numeric(e, "add", std::plus<int>{});
             }));
  e->set("-", make_shared<BuiltinFunction>(
             vector<string>{"a", "b"},
             [] (Environment&e) -> FormPtr {
               return builtin_numeric(e, "subtract", std::minus<int>{});
             }));

  e->set("*", make_shared<BuiltinFunction>(
             vector<string>{"a", "b"},
             [] (Environment&e) -> FormPtr {
               return builtin_numeric(e, "multiply", std::multiplies<int>{});
             }));

  e->set("/", make_shared<BuiltinFunction>(
             vector<string>{"a", "b"},
             [] (Environment&e) -> FormPtr {
               return builtin_divide(e, "divide", std::divides<int>{});
             }));

  e->set("%", make_shared<BuiltinFunction>(
             vector<string>{"a", "b"},
             [] (Environment&e) -> FormPtr {
               return builtin_divide(e, "mod", std::modulus<int>{});
             }));

  return e;
}

int main()
{
  auto base_env = create_base_env();
  string line;
  do
  {
    cout << prompt;
    if (!getline(cin, line)) break;
    auto readform = read(line);
    print(eval(readform, *base_env));
  } while (true);

  return 0;
}
