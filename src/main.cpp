#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>

#include "lib/nlohmann/json.hpp"

using json = nlohmann::json;
std::string getNextWord (std::string input, int &pos, const char* ch)
{
    std::string ret="";
    while (pos<input.size() && strchr(ch, input[pos]))
        ++pos;
    while (pos<input.size() && !strchr(ch, input[pos]))
        ret.append (1, input[pos++]);
    return ret;
}
json decodeBencodedInteger (std::string encoded_value, int &pos)
{
    ++pos;
    long long val = atoll(getNextWord(encoded_value, pos, "e").c_str());
    /// pos ends on 'e'
    ++pos;
    return json(val);
}
json decodeBencodedString (std::string encoded_value, int &pos)
{
    int sz = atoi(getNextWord(encoded_value, pos, ":").c_str());
    /// pos ends on ':'
    ++pos;
    std::string nxt="";
    while (pos<encoded_value.size() && sz--)
        nxt+=encoded_value[pos++];
    return json(nxt);
}
json decodeBencodedList (std::string encoded_value, int &pos)
{
    json ret=json::array();
    ++pos;
    while (pos<encoded_value.size() && encoded_value[pos]!='e')
    {
        if (encoded_value[pos]=='i')
            ret.push_back(decodeBencodedInteger(encoded_value, pos));
        if (isdigit(encoded_value[pos]))
            ret.push_back(decodeBencodedString(encoded_value, pos));
        if (encoded_value[pos]=='l')
            ret.push_back(decodeBencodedList(encoded_value, pos));
    }
    ++pos;
    return ret;
}
json decodeAll (std::string encoded_value)
{
    json ret;
    int pos=0;
    while (pos<encoded_value.size())
    {
        if (encoded_value[pos]=='l')
            ret=decodeBencodedList(encoded_value, pos);
        if (isdigit(encoded_value[pos]))
            ret=decodeBencodedString(encoded_value, pos);
        if (encoded_value[pos]=='i')
            ret=decodeBencodedInteger(encoded_value, pos);
    }
    return ret;
}
int main(int argc, char* argv[]) {
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
        return 1;
    }

    std::string command = argv[1];

    if (command == "decode") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
            return 1;
        }
        // You can use print statements as follows for debugging, they'll be visible when running tests.
        std::cerr << "Logs from your program will appear here!" << std::endl;

        std::string encoded_value = argv[2];
        json decoded_value = decodeAll(encoded_value);
        std::cout << decoded_value.dump() << std::endl;
    } else {
        std::cerr << "unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
