#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <openssl/sha.h>
#include "lib/nlohmann/json.hpp"

using json = nlohmann::json;
json decodeBencodedList (std::string encoded_value, int &pos);
json decodeBencodedDictionary (std::string encoded_value, int &pos);
json decodeBencodedInteger (std::string encoded_value, int &pos);
json decodeBencodedString (std::string encoded_value, int &pos);
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
json decodeBencodedDictionary (std::string encoded_value, int &pos)
{
    json ret=json::object();
    ++pos;
    while (pos<encoded_value.size() && encoded_value[pos]!='e')
    {
        json pair[2];
        for (int i=0;i<=1;i++)
        {
            if (encoded_value[pos]=='i')
                pair[i]=decodeBencodedInteger(encoded_value, pos);
            else if (isdigit(encoded_value[pos]))
                pair[i]=decodeBencodedString(encoded_value, pos);
            else if (encoded_value[pos]=='l')
                pair[i]=decodeBencodedList(encoded_value, pos);
            else if (encoded_value[pos]=='d')
                pair[i]=decodeBencodedDictionary(encoded_value, pos);
        }
        ret[pair[0]]=pair[1];
    }
    ++pos;
    return ret;
}
json decodeBencodedList (std::string encoded_value, int &pos)
{
    json ret=json::array();
    ++pos;
    while (pos<encoded_value.size() && encoded_value[pos]!='e')
    {
        if (encoded_value[pos]=='i')
            ret.push_back(decodeBencodedInteger(encoded_value, pos));
        else if (isdigit(encoded_value[pos]))
            ret.push_back(decodeBencodedString(encoded_value, pos));
        else if (encoded_value[pos]=='l')
            ret.push_back(decodeBencodedList(encoded_value, pos));
        else if (encoded_value[pos]=='d')
            ret.push_back(decodeBencodedDictionary(encoded_value, pos));
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
        if (encoded_value[pos]=='d')
            ret=decodeBencodedDictionary(encoded_value, pos);
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
    }
    else if (command == "info")
    {
        FILE *f;
        if (f=fopen (argv[2], "r"))
        {
            char buff[4096];
            ssize_t n;
            std::string input="";
            while ((n=fread(buff, sizeof(char), 4096, f)) > 0)
            {
                for (int i=0;i<n;i++)
                    input.append(1, buff[i]);
            }
            /// i will avoid bencoding the info dict again and instead i will just search its encoding in the input string
            int pos=0;
            for (int i=0;i<input.size()-3;i++)
            {
                if (input.substr(i, 4)=="info")
                {
                    pos=i+4;
                    break;
                }
            }
            std::string to_hash = input.substr(pos, (int)input.size()-1-pos);
            json decoded_value = decodeAll(input);
            std::cout << "Tracker URL: " << decoded_value["announce"].dump().substr(1, (int)decoded_value["announce"].dump().size() - 2) << "\nLength: " << decoded_value["info"]["length"].dump() << "\n";
            unsigned char hash[SHA_DIGEST_LENGTH]="";
            std::cout << input << "\n";
            std::cout << to_hash << "\n";
            SHA1(reinterpret_cast<const unsigned char*>(to_hash.data()), to_hash.size(), hash);
            std::stringstream oss;
            for (auto& c : hash)
                oss << std::setw(2) << std::setfill('0') << std::hex << (int)c;
            std::cout << "Info Hash: " << oss.str() << "\n";
        }
        else
            std::cerr << "Can't open info file\n";
    }
    else {
        std::cerr << "unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
