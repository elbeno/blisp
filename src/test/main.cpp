#include <iostream>
#include <string>

using namespace std;

static const char *prompt = "blisp> ";

auto read(const string& s)
{
  return s;
}

auto eval(const string& form)
{
  return form;
}

void print(const string& s)
{
  cout << s << endl;
}

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
