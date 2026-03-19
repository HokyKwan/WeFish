#include "connection.h"
#include "voice.h"


std::string request_str = R"({"model": "deepseek-chat", "messages": [], "stream": false})";

std::string voice_access_token = "24.16c198c9d94b60b9ac09300b19788328.2592000.1742472983.282335-117591522";

cJSON *root = NULL;

void Pack(std::string content)
{
    cJSON *array = cJSON_GetObjectItem(root, "messages");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", content.c_str());
    cJSON_AddItemToArray(array, msg);
}

void Response(std::string path, std::string content)
{
    if (!path.compare("unknown")) {

    } else {
        std::cout << "\t\t\t\t\t\t\t\t\t\t[DeepSeek]: " << content << std::endl;
        Pack(content);
    }
}

void VoiceResponse(std::string path, std::string content)
{
    if (!path.compare("unknown")) {

    } else if (!path.compare("taskid")) {
        std::cout << "Task ID: " << content << std::endl;
    } else if (!path.compare("accesstoken")) {
        std::cout << "Access Token: " << content << std::endl;
    } else {
        std::cout << content << std::endl;
    }
}

int main(void)
{
    std::string line;
    root = cJSON_Parse(request_str.c_str());

    std::cout << "#################### DeepSeek Chat ####################\n";
    try {
        while (1) {
            std::cout << "[Me]: ";
            std::getline(std::cin, line);
            if (line.empty()) {
                return -1;
            }

            boost::asio::io_context ioc;
            boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_client);
            // auto connection = std::make_shared<Connection>(ioc, ctx);
            auto voice = std::make_shared<Voice>(ioc, ctx);

            // Pack(line);
            // std::cout << cJSON_Print(root) << "\n";

            // connection->Start(cJSON_Print(root), std::bind(&Response, std::placeholders::_1, std::placeholders::_2));
            Request req;
            req.Method(Request::verb::post);
            req.Target("/oauth/2.0/token?grant_type=client_credentials&client_id=kLzZKVQqJoF3edLNRjfebuVB&client_secret=8UAm78zRmIKb5CbOLvT2tzkpgEHyIn6w");
            req.Version(11);
            req.Set(Request::field::host, "aip.baidubce.com");
            req.Set(Request::field::content_type, "application/json");
            req.Set(Request::field::accept, "application/json");
            req.Body() = "";
            voice->Start(req, std::bind(&VoiceResponse, std::placeholders::_1, std::placeholders::_2));

            // auto voice2 = std::make_shared<Voice>(ioc, ctx);
            // Request req2;
            // req2.Method(Request::verb::post);
            // req2.Target("/rpc/2.0/tts/v1/create?access_token=" + voice_access_token);
            // req2.Version(11);
            // req2.Set(Request::field::host, "aip.baidubce.com");
            // req2.Set(Request::field::content_type, "application/json");
            // req2.Set(Request::field::accept, "application/json");
            // std::string body = R"({"text":"你好，我是关贺基","format":"mp3-48k","voice":0,"lang":"zh","speed":5,"pitch":5,"volume":5,"enable_subtitle":0})";
            // req2.Set(Request::field::content_length, std::to_string(body.length()));
            // req2.Body() = body;
            // voice2->Start(req2, std::bind(&VoiceResponse, std::placeholders::_1, std::placeholders::_2));

            ioc.run();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}