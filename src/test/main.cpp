#include <algorithm>
#include <cstddef>
#include <iostream>
#include <iterator>
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

  vector<Token> m_tokens;
  size_t pos = 0;
};

//------------------------------------------------------------------------------

struct Form
{
  virtual ~Form() {}
  virtual const Form eval() { return *this; }
  virtual string print() const { return "<form>"; }
};

//------------------------------------------------------------------------------

auto read(const string&)
{
  return Form{};
}

auto eval(const Form& form)
{
  return form;
}

void print(const Form& form)
{
  cout << form.print() << endl;
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
    auto evalform = eval(readform);
    print(evalform);

  } while (true);

  return 0;
}
