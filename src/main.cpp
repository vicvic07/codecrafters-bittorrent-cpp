#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <unistd.h>
#include "lib/nlohmann/json.hpp"

using json = nlohmann::json;
unsigned char info_dict_hash[SHA_DIGEST_LENGTH];
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
std::string extractTorrentInfo (FILE *f)
{
    char buff[4096];
    ssize_t n;
    std::string input="";
    while ((n=fread(buff, sizeof(char), 4096, f)) > 0)
    {
        for (int i=0;i<n;i++)
            input.append(1, buff[i]);
    }
    return input;
}
std::string getInfoHash (std::string input)
{
    int pos=0;
    /// i will avoid bencoding the info dict again and instead i will just search its encoding in the input string
    for (int i=0;i<input.size()-3;i++)
    {
        if (input.substr(i, 4)=="info")
        {
            pos=i+4;
            break;
        }
    }
    std::string to_hash = input.substr(pos, (int)input.size()-1-pos);
    SHA1(reinterpret_cast<const unsigned char*>(to_hash.data()), to_hash.size(), info_dict_hash);
    std::stringstream oss;
    for (auto& c : info_dict_hash)
        oss << std::setw(2) << std::setfill('0') << std::hex << (int)c;
    return oss.str();
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
            std::string input = extractTorrentInfo(f);
            std::string hash = getInfoHash(input);
            json decoded_value = decodeAll(input);
            std::cout << "Tracker URL: " << decoded_value["announce"].dump().substr(1, (int)decoded_value["announce"].dump().size() - 2) << "\nLength: " << decoded_value["info"]["length"].dump() << "\n";
            std::cout << "Info Hash: " << hash << "\nPiece Length: " << decoded_value["info"]["piece length"].dump() << "\nPieces: ";
            std::stringstream oss;
            for (const unsigned char &c : decoded_value["info"]["pieces"].get<std::string>())
                oss << std::setw(2) << std::setfill('0') << std::hex << (int)c;
            std::cout << oss.str() << "\n";
        }
        else
            std::cerr << "Can't open info file\n";
    }
    else if (command == "peers")
    {
        FILE *f;
        if (f=fopen (argv[2], "r"))
        {
            std::string input = extractTorrentInfo(f);
            json decoded_value=decodeAll(input);
            std::string reversed_url=decoded_value["announce"].dump().substr(1, (int)decoded_value["announce"].dump().size()-2);
            std::reverse(reversed_url.begin(), reversed_url.end());
            int pos=0;
            std::string path = getNextWord(reversed_url, pos, "/");
            std::string url = getNextWord(reversed_url, pos, "");
            std::reverse(url.begin(), url.end());
            path+='/';
            std::reverse (path.begin(), path.end());
            int pos3=0;
            std::string hash_hex=getInfoHash(input);
            std::string hash="";
            for (int i=0;i<hash_hex.size()-1;i+=2)
                hash.append(1, '%'), hash.append(1, hash_hex[i]), hash.append(1, hash_hex[i+1]);
            std::string header="GET "+path+"?info_hash="+hash+"&peer_id="+"67676767676767676767&port=6881&uploaded=0&downloaded=0&left="+decoded_value["info"]["length"].dump()+"&compact=1 "+"HTTP/1.1\r\nHost: "+getNextWord(url, pos3, "htp:/");
            header+=":"+getNextWord(url, pos3, ":\r\n/")+"\r\nUser-Agent: BitTorrent-Peer\r\nAccept: */*\r\n";
            std::string body="\r\n";
            std::cerr << header+body << "\n";
            int pos2=0;
            std::string addr = getNextWord(url, pos2, "htp:/");
            std::string port = getNextWord(url, pos2, "/:\n\r");
            int tracker_fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in tracker;
            tracker.sin_family = AF_INET;
            tracker.sin_addr.s_addr = inet_addr(addr.c_str());
            tracker.sin_port = htons(atoi(port.c_str()));
            if (connect (tracker_fd, (struct sockaddr*)&tracker, sizeof(tracker))>=0)
            {
                if (ssize_t n=write (tracker_fd, (header+body).c_str(), (header+body).size()))
                {
                    char buff[1024];
                    ssize_t bytes=read (tracker_fd, buff, sizeof(buff));
                    int pos=0;
                    while (!(buff[pos]=='\r' && buff[pos+1]=='\n' && buff[pos+2]=='\r' && buff[pos+3]=='\n'))
                        ++pos;
                    pos+=4;
                    std::cout << pos << " " << buff+pos << "\n";
                    json decoded_response = decodeAll(buff+pos);
                    std::string sockets=decoded_response["peers"].get<std::string>();
                    for (int i=0;i<sockets.size();i+=6)
                    {
                        int v[4]={(int)(unsigned char)sockets[i], (int)(unsigned char)sockets[i+1], (int)(unsigned char)sockets[i+2], (int)(unsigned char)sockets[i+3]};
                        int port=(int)(unsigned char)sockets[i+4]*(1 << 8)+(int)(unsigned char)sockets[i+5];
                        for (int i=0;i<4;i++)
                        {
                            std::cout << v[i];
                            if (i!=3)
                                std::cout << ".";
                        }
                        std::cout << ":" << port << "\n";
                    }
                }
                else
                    std::cerr << "Write to tracker failed!\n";

            }
            else
                std::cerr << "Connection to tracker failed!\n";
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
