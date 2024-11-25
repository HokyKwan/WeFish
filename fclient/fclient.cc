/*
 *       WeFish File Client
 *
 *  Author: <hoky.guan@tymphany.com>
 *
 */
#include <cstdlib>
#include <deque>
#include <thread>
#include <boost/asio.hpp>
#include "fclient.hpp"
#include <stdio.h>
#include <utility>

#include "base64.hpp"
#include "cryptor.h"

int account_s = 0;

void showProgressBar(int progress, int total) {
    const int barWidth = 70;

    float progressRatio = static_cast<float>(progress) / total;
    int progressBarLength = static_cast<int>(progressRatio * barWidth);

    std::cout << "[";
    for (int i = 0; i < progressBarLength; ++i) {
        std::cout << "=";
    }
    for (int i = progressBarLength; i < barWidth; ++i) {
        std::cout << " ";
    }
    std::cout << "] " << static_cast<int>(progressRatio * 100.0) << "%\r";
    std::cout.flush();
}

Connection::Connection(boost::asio::io_context& io_context, tcp::resolver::results_type& endpoints)
    : io_context_(io_context), strand_(io_context), socket_(io_context), endpoints_(endpoints)
{
    key_ = "ba3483abc1af7e9d0cf2325010ed76d7";
}

void Connection::doConnect(const ResponseHandler& handler)
{
    boost::asio::async_connect(socket_, endpoints_,
        [this, self = shared_from_this(), handler](const boost::system::error_code& ec, tcp::endpoint) {
            std::string ret;
            if (!ec && handler) {
                ret = "Connected";
                handler(ec, ret);
            }
        });
}

void Connection::doDisconnect()
{
    std::cout << "Disconnecting\n";
    if (!socket_.is_open()) {
        std::cerr << "Not connected\n";
        return;
    }
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    if (ec) {
        std::cout << " Error in socket shutdown: " << ec.message() << "\n";
    }
    socket_.close(ec);
    if (ec)
        std::cout << "Error in socket close: " << ec.message() << "\n";
    std::cout << "Disconnected\n";
    std::cout << "\n * Enter to quit * \n";
    io_context_.stop();
}

void Connection::doRead(const ResponseHandler& handler)
{
    const std::string delimiter = "\n";
    boost::asio::async_read_until(
        socket_, streambuf_, delimiter,
        boost::asio::bind_executor(strand_, [this, self = shared_from_this(), delimiter, handler](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                if (handler) {
                    std::string ret = "Socket Error Occurred";
                    handler(ec, ret);
                }
                return;
            }
            std::string line{buffers_begin(streambuf_.data()), buffers_begin(streambuf_.data()) + bytes_transferred - delimiter.length()};
            if (!line.empty()) {
                if (line.back() == '\r')
                    line.resize(line.size() - 1);

                if (!line.empty()) {
                    if (handler) {
                        std::string decrypt = AESDecrypt(line, key_);
                        std::string decoded = decrypt.substr(0, decrypt.find("}}") + 2);
                        handler(ec, decoded);
                    }
                }
            }
            streambuf_.consume(bytes_transferred);
            doRead(handler);
        }));
}

void Connection::SendAsync(const std::string& msg)
{
    std::string encoded = AESEncrypt(msg, key_);
    strand_.post([this, self = shared_from_this(), encoded]() {
        messages_.emplace_back(encoded + "\r\n");
        if (messages_.size() > 1) {
            // std::cout << "TCP session async_writes: " << messages_.size() << "\n";
            return;
        }
        doWrite();
    });
}

void Connection::doWrite()
{
    boost::asio::async_write(socket_, boost::asio::buffer(messages_.front()),
         boost::asio::bind_executor(strand_, [this, self = shared_from_this()] (std::error_code ec, std::size_t length) {
             messages_.pop_front();
             if (ec) {
                 std::cout << "Client: Error while writing to socket: " << ec.message() << "\n";
                 socket_.close();
             }
             if (!messages_.empty())
                 doWrite();
         }));
}

FileStream::FileStream(std::string& name) : name_(name)
{
}

void FileStream::Push(std::string& content)
{
    contents_.append(content);
}

std::string FileStream::Pop(void)
{
    std::string pop = contents_;
    contents_ = std::string();
    return pop;
}

int FileStream::Size(void)
{
    return contents_.size();
}

std::string FileStream::Name(void)
{
    return name_;
}

FClient::FClient(boost::asio::io_context& io_context, tcp::resolver::results_type& endpoints)
{
    connection_ = std::make_shared<Connection>(io_context, endpoints);
    Start();
}

void FClient::Start()
{
    connection_->doConnect([this] (const boost::system::error_code& ec, std::string& response) {
        if (!ec) {
            if (response == "Connected") {
                doMessageReceived();
            }
        }
    });
}

void FClient::doMessageReceived()
{
    connection_->doRead([this] (const boost::system::error_code& ec, std::string& response) {
        if (ec) {
            std::cerr << "Server Lost\n";
            Stop();
        } else {
            //std::cout << "Response: " << response << " , Length: " << response.size() << "\n";
            jsonrpcpp::entity_ptr entity(nullptr);
            entity = jsonrpcpp::Parser::do_parse(response);
            if (entity->is_response()) {
                jsonrpcpp::response_ptr response = std::dynamic_pointer_cast<jsonrpcpp::Response>(entity);
                jsonrpcpp::parameter_ptr param = std::make_shared<jsonrpcpp::Parameter>(response->result());
                if (param->get("Method") == "SayHello") {
                    int account = param->get("Account");
                    std::cout << "Account: " << std::to_string(account) << " Online" << std::endl;
                } else if (param->get("Method") == "VersionChecked") {
                    int Expired = param->get("Expired");
                    if (Expired) {
                        jsonrpcpp::request_ptr request(nullptr);
                        request.reset(new jsonrpcpp::Request(jsonrpcpp::Id(MESSAGE_TYPE_SETTING), "UpgradeRequest",
                            jsonrpcpp::Parameter("Account", account_s)));
                        connection_->SendAsync(request->to_json().dump());
                    }
                } else if (param->get("Method") == "UpgradeReply") {
                    std::string file_name = param->get("Filename");
                    std::cout << "File Name: " << file_name << std::endl;

                    upgrade_stream_.reset(new FileStream(file_name));

                    jsonrpcpp::request_ptr request(nullptr);
                    request.reset(new jsonrpcpp::Request(jsonrpcpp::Id(MESSAGE_TYPE_FILE), "UpgradeFileTransfer",
                        jsonrpcpp::Parameter("Account", account_s)));
                    connection_->SendAsync(request->to_json().dump());

                } else if (param->get("Method") == "UpgradeProcessing") {
                    int process = param->get("Process");
                    int length = param->get("Length");
                    std::string checksum = param->get("Checksum");

                    //std::cout << "Process: " << std::to_string(process) << std::endl;
                    std::string content = param->get("Content");
                    std::string decrypt = CBASE64::Decode(content);
                    showProgressBar(process , 100);
                    decrypt.resize(length);
                    upgrade_stream_->Push(decrypt);

                    if (process < 100) {
                        jsonrpcpp::request_ptr request(nullptr);
                        request.reset(new jsonrpcpp::Request(jsonrpcpp::Id(MESSAGE_TYPE_FILE), "UpgradeFileTransfer",
                            jsonrpcpp::Parameter("Account", account_s)));
                        connection_->SendAsync(request->to_json().dump());
                    } else {
                        std::fstream file_stream;
                        file_stream.open(upgrade_stream_->Name(), std::ios::out | std::ios::binary);
                        file_stream.write(upgrade_stream_->Pop().c_str(), upgrade_stream_->Size());
                        std::string file_name = upgrade_stream_->Name();
                        upgrade_stream_ = nullptr;
                        file_stream.close();
                        std::string md5sum = MD5Encrypt(file_name);
                        if (md5sum.compare(checksum) == 0) {
                            std::cout << "\nUpgrade Successfully\n";
                        } else {
                            std::cout << "\nChecksum: " << checksum << " Md5sum: " << md5sum << "\n";
                        }
                    }
                }
            } else if (entity->is_notification()) {
                jsonrpcpp::notification_ptr notification = std::dynamic_pointer_cast<jsonrpcpp::Notification>(entity);
                if (notification->method() == "FileTransferNotification") {
                    int from_account = notification->params().get("Account");
                    std::string name = notification->params().get("Name");
                    int to_account = notification->params().get("ToAccount");
                    std::string file_name = notification->params().get("Filename");
                    std::string checksum = notification->params().get("Checksum");
                    size_t file_size = notification->params().get("Filesize");
                    std::string content = notification->params().get("Content");
                    std::string decrypt = CBASE64::Decode(content);
                    int status = notification->params().get("Status");

                    // std::cout << "Account: " << std::to_string(from_account) << std::endl;
                    // std::cout << "ToAccount: " << std::to_string(to_account) << std::endl;
                    // std::cout << "Filename: " << file_name << std::endl;
                    // std::cout << "Filesize: " << std::to_string(file_size) << std::endl;
                    // std::cout << "Checksum: " << checksum << std::endl;
                    // std::cout << "Content: " << content << std::endl;
                    // std::cout << "Status: " << std::to_string(status) << std::endl;

                    int process;
                    auto it = container_.find(checksum);
                    if (status == 1 && it == container_.end()) {
                        std::shared_ptr<FileStream> file_stream = std::make_shared<FileStream>(file_name);
                        container_.insert(std::pair<std::string, std::shared_ptr<FileStream>>(checksum, file_stream));
                    } else if (status == 2 && it != container_.end()) {
                        (it->second)->Push(decrypt);
                        process = (((size_t)(it->second)->Size() * 100)) / file_size;
                    } else if (status == 0 && it != container_.end()) {
                        (it->second)->Push(decrypt);
                        process = (((size_t)(it->second)->Size() * 100)) / file_size;
                        std::fstream file_stream;
                        std::string fileName = file_name + "_rev";
                        file_stream.open(fileName, std::ios::out | std::ios::binary);
                        file_stream.write((it->second)->Pop().c_str(), (it->second)->Size());
                        file_stream.close();
                        container_.erase(it);
                    }
                    showProgressBar(process , 100);
                }
            }
        }
    });
}

void FClient::Send(const std::string& msg)
{
    connection_->SendAsync(msg);
}

void FClient::Stop()
{
    connection_->doDisconnect();
}

void FClient::doFileSection(std::string filename, int sequence, std::streampos start, std::streampos sectionSize)
{
    std::fstream file;
    file.open(filename, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return;
    }
    file.seekg(start);

    std::string sectionData;
    sectionData.resize(sectionSize);
    file.read(&sectionData[0], sectionSize);
    std::unique_lock<std::mutex> lock(mutex_);
    v_section_[sequence] = sectionData;
}

void FClient::SendFile(std::string& file_name)
{
    bool head = true;
    std::string file_content;
    std::fstream file_stream;
    file_stream.open(file_name, std::ios::in | std::ios::binary);
    if (!file_stream.is_open()) {
        std::cerr << "Could not create file" << std::endl;
        return;
    }
    file_stream.seekg(0, std::ios::end);
    size_t file_size = file_stream.tellg();
    file_stream.seekg(0, std::ios::beg);

#define THREAD_NUM 4
    v_section_.resize(THREAD_NUM);
    std::streampos sizeConsume = 0;
    std::streampos sizePerBloack = file_size / THREAD_NUM;
    std::vector<std::thread> v_thread;
    for (int i = 0; i < THREAD_NUM; ++i) {
        std::streampos start = i * sizePerBloack;
        if (i == THREAD_NUM - 1) {
            sizePerBloack = file_size - sizeConsume;
        }
        sizeConsume += sizePerBloack;
        v_thread.emplace_back(&FClient::doFileSection, this, file_name, i, start, sizePerBloack);
    }
    for (auto& thread : v_thread) {
        thread.join();
    }

    for (auto &it : v_section_) {
        file_content.append(it);
        it = std::string();
    }

    jsonrpcpp::request_ptr request(nullptr);
    std::string checksum = "b60b0ce5bbab49f5ec134022ed7a908e";
    int account = account_s;
    int to_account = 0;
    int data_consume = 0;

    while (file_content.size()) {
        if (head) {
            head = false;
            request.reset(new jsonrpcpp::Request(jsonrpcpp::Id(MESSAGE_TYPE_FILE), "FileTransfer",
                jsonrpcpp::Parameter("Account", account_s, "Name", "Fclient", "ToAccount", to_account, "Status", 1,
                                    "Filename", file_name, "Filesize", file_size, "Checksum", checksum,
                                    "Content", "")));
        } else {
#define M_BLOCK_SIZE 3000000
            int blocksize = M_BLOCK_SIZE;
            std::string content;
            if (file_size - data_consume < blocksize) {
                blocksize = file_size - data_consume;
                content = file_content.substr(0, blocksize);
                std::string encrypt = CBASE64::Encode(content.data(), content.size());
                request.reset(new jsonrpcpp::Request(jsonrpcpp::Id(MESSAGE_TYPE_FILE), "FileTransfer",
                    jsonrpcpp::Parameter("Account", account_s, "Name", "Fclient", "ToAccount", to_account, "Status", 0,
                                        "Filename", file_name, "Filesize", file_size, "Checksum", checksum,
                                        "Content", encrypt)));
                file_content = std::string();
                data_consume += blocksize;
            } else {
                content = file_content.substr(0, blocksize);
                std::string encrypt = CBASE64::Encode(content.data(), content.size());
                request.reset(new jsonrpcpp::Request(jsonrpcpp::Id(MESSAGE_TYPE_FILE), "FileTransfer",
                    jsonrpcpp::Parameter("Account", account_s, "Name", "Fclient", "ToAccount", to_account, "Status", 2,
                                        "Filename", file_name, "Filesize", file_size, "Checksum", checksum,
                                        "Content", encrypt)));
                file_content.erase(0, blocksize);
                data_consume += blocksize;
            }
        }
        Send(request->to_json().dump());
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) return -1;

    try {
        boost::asio::io_context io_context;
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve("localhost", "26690");
        FClient fclient(io_context, endpoints);

        account_s = atoi(argv[2]);

        jsonrpcpp::request_ptr request(nullptr);
        request.reset(new jsonrpcpp::Request(jsonrpcpp::Id(MESSAGE_TYPE_SETTING), "SayHello",
            jsonrpcpp::Parameter("Account", account_s)));
        fclient.Send(request->to_json().dump());
        std::cout << request->to_json().dump() << std::endl;

        request.reset(new jsonrpcpp::Request(jsonrpcpp::Id(MESSAGE_TYPE_SETTING), "VersionCheck",
            jsonrpcpp::Parameter("Clientversion", "D_20240307_1")));
        fclient.Send(request->to_json().dump());
        std::cout << request->to_json().dump() << std::endl;

        if (atoi(argv[1]) == 1) {
            std::thread sendThread([&fclient]() {
                std::string filename = "Chains.wav";
                fclient.SendFile(filename);
            });
            sendThread.detach();
        }

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM, SIGPIPE);
        signals.async_wait([&](const boost::system::error_code& ec, int signal) {
            if (!ec) {
                switch (signal) {
                case SIGINT:
                case SIGTERM:
                    throw std::system_error(); 
                    break;
                case SIGPIPE:
                    break;
                default:
                    throw std::system_error();
                    break;
                }
            } else {
                std::cout << "Failed to wait for signal, error: " << ec.message() << "\n";
            }
            fclient.Stop();
        });

        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "\nException Occurred\n";
    }
    std::cout << "\nWeFish FClient terminated.\n";
    return 0;
}


