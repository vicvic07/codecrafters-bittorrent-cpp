#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <netdb.h>
#include <thread>
#include <queue>
#include <mutex>
#include <algorithm>

#include "lib/nlohmann/json.hpp"

using json = nlohmann::json;
unsigned char info_dict_hash[SHA_DIGEST_LENGTH];
std::string hash_hex;
json decodeBencodedList (std::string encoded_value, int &pos);
json decodeBencodedDictionary (std::string encoded_value, int &pos);
json decodeBencodedInteger (std::string encoded_value, int &pos);
json decodeBencodedString (std::string encoded_value, int &pos);
json decoded_value;
int total_size=-1;
std::string announce_url;
std::vector<std::string> pieces;
std::vector<std::pair<std::pair<std::string, int>, int>> request_queue;
std::mutex request_queue_mutex;
std::vector<std::mutex> responses_mutex;
std::vector<std::vector<std::string>> piece_scrambled_responses;
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
std::string getPeerMessage (int fd)
{
    char buff[20*1024];
    ssize_t n=0;
    std::string ret="";
    while (n<4)
    {
        ssize_t m=read(fd, buff, sizeof(buff));
        n+=m;
        for (int i=0;i<m;i++)
            ret.append(1, buff[i]);
    }
    unsigned int total_sz=0;
    for (int i=0;i<=3;i++)
        total_sz+=(1 << (8*(3-i)))*(int)(unsigned char)ret[i];
    while (n<total_sz)
    {
        ssize_t m=read(fd, buff, sizeof(buff));
        n+=m;
        for (int i=0;i<m;i++)
            ret.append(1, buff[i]);
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
    hash_hex=oss.str();
    return oss.str();
}
void initializeTorrentInfo (std::string path)
{
    FILE *f;
    if (f=fopen(path.c_str(), "r"))
    {
        std::string input = extractTorrentInfo(f);
        std::string hsh = getInfoHash(input);
        decoded_value=decodeAll(input);
        announce_url=decoded_value["announce"].dump().substr(1, (int)decoded_value["announce"].dump().size()-2);
        total_size=decoded_value["info"]["length"].get<int>();
    }
    else
        std::cerr << "Can't open path: " << path << "\n";
}
void initializeTorrentInfoMagnet (std::string input)
{
    int pos=0;
    std::string tr_link="";
    while (pos<input.size())
    {
        std::string nxt = getNextWord(input, pos, ":&=");
        if (nxt=="btih")
            hash_hex=getNextWord(input, pos, ":=&");
        else if (nxt=="tr")
            tr_link=getNextWord(input, pos, "=&");
    }
    std::string tr_link_un="";
    for (int i=0;i<tr_link.size();i++)
    {
        if (i+2<tr_link.size() && tr_link[i]=='%' && tr_link[i+1]=='2' && tr_link[i+2]=='F')
        {
            tr_link_un.append(1, '/');
            i+=2;
        }
        else if (i+2<tr_link.size() && tr_link[i]=='%' && tr_link[i+1]=='3' && tr_link[i+2]=='A')
        {
            tr_link_un.append(1, ':');
            i+=2;
        }
        else
            tr_link_un.append(1, tr_link[i]);
    }
    auto toInt = [&] (const char& x)
    {
        if (isdigit(x))
            return x-'0';
        return 10+x-'a';
    };
    for (int i=0;i<SHA_DIGEST_LENGTH;i++)
        info_dict_hash[i]=(1 << 4)*toInt(hash_hex[i << 1])+toInt(hash_hex[i << 1 | 1]);
    announce_url=tr_link_un;
}
std::string peerList ()
{
    std::string ret="";
            std::string reversed_url=announce_url, ip_str;
            std::reverse(reversed_url.begin(), reversed_url.end());
            int pos=0;
            std::string path = getNextWord(reversed_url, pos, "/");
            std::string url = getNextWord(reversed_url, pos, "");
            std::reverse(url.begin(), url.end());
            int pos4=0, port=80;
            std::string addr="";
            std::string hostname=getNextWord(url, pos4, "/");
            hostname=getNextWord(url, pos4, "/");
            pos4=0;
            int tracker_fd=socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in tracker;
            if (hostname.find(':')!=std::string::npos)
            {
                addr=getNextWord(hostname, pos4, ":");
                port=atoi(getNextWord(hostname, pos4, ":/").c_str());
                ip_str=addr+":"+std::to_string(port);
                tracker.sin_family = AF_INET;
                tracker.sin_addr.s_addr = inet_addr(addr.c_str());
                tracker.sin_port = htons(port);
            }
            else
            {
                addrinfo hints{};
                hints.ai_family=AF_UNSPEC;
                hints.ai_socktype=SOCK_STREAM;
                addrinfo *result;
                int stat = getaddrinfo(hostname.c_str(), "80", &hints, &result);
                if (stat != 0)
                {
                    std::cerr << stat << " " << hostname.c_str() << "\n";
                    std::cerr << gai_strerror(stat) << "\n";
                    _exit (1);
                }
                for (addrinfo *r=result;r!=nullptr;r=r -> ai_next)
                {
                    if (r -> ai_family==AF_INET)
                    {
                        char ip[INET6_ADDRSTRLEN];
                        auto* address = &reinterpret_cast<sockaddr_in*>(r -> ai_addr)->sin_addr;
                        tracker=*reinterpret_cast<sockaddr_in*>(r -> ai_addr);
                        port=ntohs(tracker.sin_port);
                        inet_ntop(r -> ai_family, address, ip, INET6_ADDRSTRLEN);
                        ip_str=hostname+":"+std::to_string(port);
                        break;
                    }
                }
                freeaddrinfo(result);
            }
            path+='/';
            std::reverse (path.begin(), path.end());
            int pos3=0;
            std::string hash="";
            for (int i=0;i<hash_hex.size()-1;i+=2)
                hash.append(1, '%'), hash.append(1, hash_hex[i]), hash.append(1, hash_hex[i+1]);
            std::string header="GET "+path+"?info_hash="+hash+"&peer_id="+"67676767676767676767&port=6881&uploaded=0&downloaded=0&left="+std::to_string(total_size==-1?6767:total_size)+"&compact=1 "+"HTTP/1.1\r\nHost: "+ip_str+"\r\nUser-Agent: BitTorrent-Peer\r\nAccept: */*\r\n";
            std::string body="\r\n";
            int pos2=0;
            if (connect (tracker_fd, (struct sockaddr*)&tracker, sizeof(tracker))>=0)
            {
                if (ssize_t n=write (tracker_fd, (header+body).c_str(), (header+body).size()))
                {
                    char buff[4096];
                    ssize_t bytes=read (tracker_fd, buff, sizeof(buff));
                    while (!(buff[pos]=='\r' && buff[pos+1]=='\n' && buff[pos+2]=='\r' && buff[pos+3]=='\n'))
                        ++pos;
                    pos+=4;
                    std::string bytes_string="";
                    for (;pos<bytes;pos++)
                        bytes_string.append(1, buff[pos]);
                    json decoded_response = decodeAll(bytes_string);
                    std::string sockets=decoded_response["peers"].get<std::string>();
                    for (int i=0;i<sockets.size();i+=6)
                    {
                        int v[4]={(int)(unsigned char)sockets[i], (int)(unsigned char)sockets[i+1], (int)(unsigned char)sockets[i+2], (int)(unsigned char)sockets[i+3]};
                        int port=(int)(unsigned char)sockets[i+4]*(1 << 8)+(int)(unsigned char)sockets[i+5];
                        for (int i=0;i<4;i++)
                        {
                            ret.append(std::to_string(v[i]));
                            if (i!=3)
                                ret.append(1, '.');
                        }
                        ret.append(1, ':');
                        ret.append(std::to_string(port));
                    }
                }
                else
                    std::cerr << "Write to tracker failed!\n";

            }
            else
                std::cerr << "Connection to tracker failed!\n";
    return ret;
}
std::pair <int, std::pair<std::string, int>> peerHandshake (std::string socket_str, std::vector<int> extensions)
{
    std::pair <int, std::pair<std::string, int>> ret;
    int pos=0;
    std::string ip_string = getNextWord(socket_str, pos, ":");
    int port = atoi(getNextWord(socket_str, pos, ":\r\n \t").c_str());
    int peer_fd=-1;
    struct sockaddr_in peer;
    peer_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (peer_fd==-1)
        std::cerr << "Failed to create fd to peer\n";
    ret.first=peer_fd;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(ip_string.c_str());
    FILE *f;
    if (connect (peer_fd, (struct sockaddr*)&peer, sizeof(peer))>=0)
    {
            std::string wr;
            wr.append(1, 0b00010011);
            wr.append("BitTorrent protocol");
            wr.append(8, 0x00);
            for (const auto& bit : extensions)
            {
                int pos=bit/8;
                wr[27-pos] |=(1 << (bit%8));
            }
            wr.append(std::string(reinterpret_cast<const char*>(info_dict_hash)));
            for (int i=1;i<=20;i++)
                wr.append(1, rand()%257);
            ssize_t m=write (peer_fd, wr.c_str(), wr.size());
            char buff[4096];
            ssize_t n=read (peer_fd, buff, 68);
            bool ok=1;
            for (const auto& bit : extensions)
            {
                int pos=bit/8;
                ok &=!!(buff[27-pos] & (1 << (bit%8)));
            }
            ret.second.second=ok;
            std::stringstream oss;
            for (int i=n-20;i<n;i++)
                oss << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)buff[i];
            ret.second.first=oss.str();
    }
    else
        std::cerr << "Can't connect to peer\n";
    return ret;
}
void worker (std::string socket_str)
{
    auto ret=peerHandshake(socket_str, {});
    std::string peer_id=ret.second.first;
    int socket_fd=ret.first;
    std::string msg=getPeerMessage(socket_fd);
    std::string wr="";
    wr.append(5, 0x00);
    wr[3]=0x01;
    wr[4]=0x02;
    write (socket_fd, wr.c_str(), (unsigned int)wr.size());
    msg=getPeerMessage(socket_fd);
    while (1)
    {
        std::vector<std::pair<std::pair<std::string, int>, int>> tasks;
        {
            std::lock_guard lock(request_queue_mutex);
            for (int i=0;i<5 && !request_queue.empty();i++)
            {
                tasks.push_back(request_queue.back());
                request_queue.pop_back();
            }
        }
        for (const auto& task : tasks)
        {
            ssize_t n=write (socket_fd, task.first.first.c_str(), task.first.first.size());
            std::string response = getPeerMessage(socket_fd);
            {
                std::lock_guard lock(responses_mutex[task.second]);
                piece_scrambled_responses[task.second].push_back(response);
            }
        }
    }
}
void downloadPiece (int piece_id)
{
    int p_length=decoded_value["info"]["piece length"];
    if ((piece_id+1)*p_length>decoded_value["info"]["length"].get<int>())
        p_length=decoded_value["info"]["length"].get<int>()%decoded_value["info"]["piece length"].get<int>();
    long long offset=0;
    int block_length=(1 << 14);
    std::vector<std::pair<std::string, int>> req;
    for (int i=0;i<p_length/(1 << 14)+(p_length%(1 << 14)!=0);i++, offset+=(1 << 14))
    {
        std::string wr="";
        wr.append(5, 0x00);
        wr[4]=0x06;
        wr.append(4, 0x00);
        int piece_id_c=piece_id;
        for (int i=(int)wr.size()-1;i>=(int)wr.size()-4;i--, piece_id_c >>=8)
        {
            for (int j=0;j<=7;j++)
                wr[i] |=(piece_id_c & (1 << j));
        }
        wr.append(4, 0x00);
        long long offset_c=offset;
        for (int i=(int)wr.size()-1;i>=(int)wr.size()-4;i--, offset_c >>=8)
        {
            for (int j=0;j<=7;j++)
                wr[i] |=(offset_c & (1 << j));
        }
        int req_sz=std::min (block_length, p_length-block_length*i);
        wr.append(4, 0x00);
        int req_sz_c=req_sz;
        for (int i=(int)wr.size()-1;i>=(int)wr.size()-4;i--, req_sz_c >>=8)
        {
            for (int j=0;j<=7;j++)
                wr[i] |=(req_sz_c & (1 << j));
        }
        int total_sz=(int)wr.size()-4;
        for (int i=3;i>=0;i--, total_sz >>=8)
        {
            for (int j=0;j<=7;j++)
                wr[i] |=(total_sz & (1 << j));
        }
        req.push_back({wr, req_sz});
    }
    while (true)
    {
        std::string file="";
        {
            std::lock_guard lock(responses_mutex[piece_id]);
            piece_scrambled_responses[piece_id].clear();
        }
        {
            std::lock_guard lock(request_queue_mutex);
            for (const auto& ind : req)
            {
                request_queue.push_back({ind, piece_id});
            }
        }
        while (1)
        {
            std::lock_guard lock(request_queue_mutex);
            if (piece_scrambled_responses[piece_id].size()==req.size())
                break;
        }
        std::cout << piece_id << std::endl;
        std::lock_guard lock(responses_mutex[piece_id]);
        std::sort (piece_scrambled_responses[piece_id].begin(), piece_scrambled_responses[piece_id].end(), [&] (std::string a, std::string b)
        {
            long long a_offset=0, b_offset=0;
            for (int i=9;i<13;i++)
            {
                for (int j=0;j<=7;j++)
                    a_offset |=(1 << 8*(12-i))*((1 << j) & a[i]), b_offset |=(1 << 8*(12-i))*((1 << j) & b[i]);
            }
            return a_offset<b_offset;
        });
        for (const auto& str : piece_scrambled_responses[piece_id])
            file.append(str.substr(13, (int)str.size()-13));
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(file.data()), file.size(), hash);
        std::stringstream oss;
        for (int i=0;i<SHA_DIGEST_LENGTH;i++)
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        std::string hex_hash=oss.str();
        std::stringstream oss2;
        for (int i=piece_id*20;i<(piece_id+1)*20;i++)
            oss2 << std::hex << std::setfill('0') << std::setw(2) << (int)(unsigned char)decoded_value["info"]["pieces"].get<std::string>()[i];
        if (oss2.str()==hex_hash)
        {
            pieces[piece_id]=file;
            return;
        }
        {
            std::lock_guard lock(request_queue_mutex);
            for (int i=0;i<request_queue.size();i++)
            {
                if (request_queue[i].second==piece_id)
                    request_queue.erase(std::next(request_queue.begin(), i)), --i;
            }
        }
    }
}
std::pair<int, std::pair<std::string, int>> peerHandshakeExtension (std::string socket)
{
    auto ret=peerHandshake(socket, {20});
    int peer_fd=ret.first;
    std::string peer_id=ret.second.first;
    int supports_all_extensions=ret.second.second;
    char buff[4096];
    read (peer_fd, buff, 6); /// bitfield
    std::string wr="";
    wr.append(4, 0x00);
    wr.append(1, 20);
    wr.append(1, 0x00);
    wr.append("d1:md11:ut_metadatai67eee");
    int sz=(int)wr.size()-4;
    for (int i=3;i>=0;i--, sz >>=8)
    {
        for (int j=0;j<8;j++)
            wr[i] |=(sz & (1 << j));
    }
    if (supports_all_extensions)
    {
        write (peer_fd, wr.c_str(), wr.size());
        std::string recv_hand;
        ssize_t n=read(peer_fd, buff, sizeof(buff));
        for (int i=0;i<n;i++)
            recv_hand.append(1, buff[i]);
        decoded_value = decodeAll(recv_hand.substr(6, (int)recv_hand.size()-6));
    }
    return ret;
}
std::string hexHashOf (std::string input)
{
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
    std::stringstream oss;
    for (int i=0;i<SHA_DIGEST_LENGTH;i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    std::cerr << oss.str() << " " << hash_hex << "\n";
    return oss.str();
}
int main(int argc, char* argv[])
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
        return 1;
    }

    std::string command = argv[1];
    auto getSockets = [&] (std::string peer_list)
    {
        std::vector<std::string> sockets;
        int pos=0;
        while (pos<peer_list.size())
        {
            std::string ip=getNextWord(peer_list, pos, ":");
            int port=0;
            ++pos;
            for (int i=pos;i<pos+5;i++)
                port=port*10+peer_list[i]-'0';
            pos=pos+5;
            sockets.push_back(ip+":"+std::to_string(port));
        }
        return sockets;
    };
    if (command == "decode") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " decode <encoded_value>" << std::endl;
            return 1;
        }
        // You can use print statements as follows for debugging, they'll be visible when running tests.
        std::cerr << "Logs from your program will appear here!" << std::endl;

        std::string encoded_value = argv[2];
        decoded_value = decodeAll(encoded_value);
        std::cout << decoded_value.dump() << std::endl;
    }
    else if (command == "info")
    {
        FILE *f;
        if (f=fopen (argv[2], "r"))
        {
            std::string input = extractTorrentInfo(f);
            std::string hash = getInfoHash(input);
            decoded_value = decodeAll(input);
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
        initializeTorrentInfo(argv[2]);
        std::cout << peerList() << "\n";
    }
    else if (command == "handshake")
    {
        initializeTorrentInfo(argv[2]);
        std::cout << "Peer ID: ";
        std::cout << peerHandshake(argv[3], {}).second.first << "\n";
    }
    else if (command == "download_piece" || command == "download")
    {
        initializeTorrentInfo(argv[4]);
        std::string peer_list=peerList();
        auto sockets=getSockets(peer_list);
        for (const auto& socket : sockets)
        {
            std::thread th(worker, socket);
            th.detach();
        }
        std::ofstream g (argv[3], std::ios::binary);
        int nr_pieces = (decoded_value["info"]["length"].get<int>()+decoded_value["info"]["piece length"].get<int>()-1)/decoded_value["info"]["piece length"].get<int>();
        pieces.resize(nr_pieces);
        piece_scrambled_responses.resize(nr_pieces);
        std::vector<std::mutex> list(nr_pieces);
        responses_mutex.swap(list);
        if (command == "download_piece")
        {
            downloadPiece (atoi(argv[5]));
            g.write(pieces[atoi(argv[5])].data(), pieces[atoi(argv[5])].size());
        }
        else
        {
            std::vector<std::thread> threads;
            for (int i=0;i<nr_pieces;i++)
            {
                std::thread req_piece (downloadPiece, i);
                req_piece.join();
            }
            for (int i=0;i<nr_pieces;i++)
                g.write(pieces[i].data(), pieces[i].size());
        }
    }
    else if (command == "magnet_parse")
    {
        initializeTorrentInfoMagnet(argv[2]);
        std::cout << "Tracker URL: " << announce_url << "\nInfo Hash: " << hash_hex << "\n";
    }
    else if (command == "magnet_handshake")
    {
        initializeTorrentInfoMagnet(argv[2]);
        std::string peer_list=peerList();
        auto sockets = getSockets(peer_list);
        auto ret = peerHandshakeExtension(sockets[0]);
        int peer_fd=ret.first;
        std::string peer_id=ret.second.first;
        std::cout << "Peer ID: " << peer_id << "\n";
        std::cout << "Peer Metadata Extension ID: " << decoded_value["m"]["ut_metadata"].dump() << "\n";
    }
    else if (command == "magnet_info")
    {
        initializeTorrentInfoMagnet(argv[2]);
        std::string peer_list=peerList();
        auto sockets = getSockets(peer_list);
        auto ret = peerHandshakeExtension(sockets[0]);
        int peer_fd=ret.first;
        std::string wr="";
        wr.append(4, 0x00);
        wr.append(1, 20);
        wr.append(1, decoded_value["m"]["ut_metadata"].get<int>());
        wr.append("d8:msg_typei0e5:piecei0ee");
        int sz = wr.size()-4;
        for (int i=3;i>=0;i--, sz >>=8)
        {
            for (int j=0;j<=7;j++)
                wr[i] |=((1 << j) & sz);
        }
        write (peer_fd, wr.c_str(), wr.size());
        char buff[4096];
        ssize_t n=read(peer_fd, buff, sizeof(buff));
        std::string info="";
        for (int i=0;i<n;i++)
            info.append(1, buff[i]);
        decoded_value = decodeAll(info.substr(6, (int)info.size()-6));
        std::string info_contents;
        int pos=0;
        for (int i=0;i<info.size()-6;i++)
        {
            if (info.substr (i, 6)=="length")
            {
                pos=i-3;
                break;
            }
        }
        std::cerr << info.substr(pos, (int)info.size()-pos) << "\n";
        if (hash_hex==hexHashOf(info.substr(pos, (int)info.size()-pos)))
        {
            std::cout << "Tracker URL: " << announce_url << "\nLength: " << decoded_value["length"].dump() << "\nInfo Hash: " << hash_hex << "\nPiece Length: " << decoded_value["piece length"].dump() << "\nPiece Hashes: ";
            std::stringstream oss;
            for (const auto& ch : decoded_value["pieces"].get<std::string>())
                oss << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)ch;
            std::cout << oss.str() << "\n";
        }
        else
            std::cerr << "Peer info hash is different from magnet link info hash\n";
    }
    else {
        std::cerr << "unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
