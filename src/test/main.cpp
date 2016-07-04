#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <iterator>
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

struct Form
{
  virtual ~Form() {}
  virtual const Form* eval() const { return this; }
  virtual string print() const { return "<form>"; }
};

using FormPtr = unique_ptr<Form>;

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

auto eval(const Form* form)
{
  return form->eval();
}

//------------------------------------------------------------------------------

void print(const Form* form)
{
  cout << form->print() << endl;
}

static const char *prompt = "blisp> ";

int main()
{
  string line;
  do
  {
    cout << prompt;
    if (!getline(cin, line)) break;
    auto readform = read(line);
    if (readform) {
      print(eval(readform.get()));
    }
  } while (true);

  return 0;
}
