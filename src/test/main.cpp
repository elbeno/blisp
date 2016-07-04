#include <algorithm>
#include <cctype>
#include <cstddef>
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
using FormPtr = unique_ptr<Form>;

class Environment
{
public:
  Environment() {}

  const Form* lookup(const string& s)
  {
    auto i = m_bindings.find(s);
    if (i == m_bindings.end()) return nullptr;
    return i->second;
  }

private:
  map<string, const Form*> m_bindings;
};

//------------------------------------------------------------------------------

struct Form
{
  virtual ~Form() {}
  virtual const Form* eval(Environment&) const { return this; }
  virtual string print() const { return "<form>"; }
};

struct Nil : public Form
{
  virtual string print() const { return "nil"; }
};

struct True : public Form
{
  virtual string print() const { return "true"; }
};

struct False : public Form
{
  virtual string print() const { return "false"; }
};

const Form* apply(const Form*, vector<const Form*>&)
{
  cout << "applied function" << endl;
  return nullptr;
}

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

  virtual const Form* eval(Environment& e) const
  {
    auto s = m_elements[0]->print();
    auto f = e.lookup(s);
    if (!f) {
      cout << "Unbound symbol: " << s << endl;
      return nullptr;
    }

    vector<const Form*> args;
    if (!all_of(m_elements.cbegin()+1, m_elements.cend(),
               [&args, &e] (const FormPtr& arg) {
                 auto f = arg->eval(e);
                 if (!f) return false;
                 args.push_back(f);
                 return true;
               })) {
      return nullptr;
    }
    return apply(f, args);
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

  virtual const Form* eval(Environment& e) const
  {
    auto f = e.lookup(m_value);
    if (!f) {
      cout << "Unbound symbol: " << m_value << endl;
    }
    return f;
  }

  string m_value;
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
    v.push_back(read_form(r));
    if (r.empty()) {
      cout << "Error: unterminated read (list)" << endl;
      return nullptr;
    }
  }
  r.next(); // eat close paren

  if (v.empty())
  {
    return make_unique<Nil>();
  }
  return make_unique<List>(std::move(v));
}

FormPtr read_atom(Reader& r)
{
  auto t = r.next();

  if (t[0] == '"') {
    return make_unique<String>(std::move(t));
  }
  if (isdigit(t[0])) {
    return make_unique<Number>(std::move(t));
  }
  if (t == "true") {
    return make_unique<True>();
  }
  if (t == "false") {
    return make_unique<False>();
  }
  if (t[0] == ';') {
    return nullptr;
  }
  return make_unique<Symbol>(std::move(t));
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

auto eval(const Form* form, Environment& e)
{
  return form->eval(e);
}

//------------------------------------------------------------------------------

void print(const Form* form)
{
  cout << form->print() << endl;
}

static const char *prompt = "blisp> ";

int main()
{
  Environment base_env;
  string line;
  do
  {
    cout << prompt;
    if (!getline(cin, line)) break;
    auto readform = read(line);
    if (readform) {
      auto f = eval(readform.get(), base_env);
      if (f) {
        print(f);
      }
    }
  } while (true);

  return 0;
}
