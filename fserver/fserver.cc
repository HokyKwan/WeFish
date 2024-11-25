/*
 *      WeFish File Server
 *
 *  Author: <hoky.guan@tymphany.com>
 *
 */
#include "fserver.hpp"
#include "base64.hpp"
#include "cryptor.h"

#include <csignal>
#include <thread>
#include <chrono>
#include <exception>
#include <system_error>
#include <boost/algorithm/string.hpp>


FSession::FSession(boost::asio::io_context& io_context, tcp::socket socket, Group& group)
    : socket_(std::move(socket)), strand_(io_context), group_(group)
{
    key_ = "ba3483abc1af7e9d0cf2325010ed76d7";
}

void FSession::doFileSection(int sequence, std::streampos start, std::streampos sectionSize)
{
    std::fstream file;
    std::string upgrade_file_path = std::string(RELEASE_PKG_DIR) + upgrade_file_name_;
    file.open(upgrade_file_path, std::ios::in | std::ios::binary);
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

void FSession::processRequest(const jsonrpcpp::request_ptr request, jsonrpcpp::entity_ptr& response, jsonrpcpp::notification_ptr& notification)
{
    try {
        Json result;
        if (request->id().int_id() == MESSAGE_TYPE_SETTING) {
            if (request->method() == "VersionCheck") {
                group_.Join(shared_from_this());
                std::string client_version = request->params().get("Clientversion");
                std::cout << "[File Server] Clientversion: " << client_version << std::endl;
                if (client_version != WEFISH_VERSION) {
                    result["Expired"] = 1;
                } else {
                    result["Expired"] = 0;
                }
                result["Method"] = "VersionChecked";
                response.reset(new jsonrpcpp::Response(*request, result));
            } else if (request->method() == "SayHello") {
                account_ = request->params().get("Account");
                std::cout << "[File Server] Account " << std::to_string(account_) << " online" << std::endl;
                result["Method"] = "SayHello";
                result["Account"] = account_;
                response.reset(new jsonrpcpp::Response(*request, result)); 
            } else if (request->method() == "UpgradeRequest") {
                upgrade_file_name_ = std::string(UPGRADE_FILE_NAME) + "_" + std::string(WEFISH_VERSION) + std::string(UPGRADE_FILE_SUFFIX);
                std::string upgrade_file_path = std::string(RELEASE_PKG_DIR) + upgrade_file_name_;
                std::cout << "[File Server] Upgrade Pkg Path: " << upgrade_file_path << "\n";
                upgrade_file_checksum_ = MD5Encrypt(upgrade_file_path);

                std::fstream file_stream;
                file_stream.open(upgrade_file_path, std::ios::in | std::ios::binary);
                if (!file_stream.is_open()) {
                    std::cerr << "Upgrade package: " << upgrade_file_path << " not found" << std::endl;
                    return;
                }
                file_stream.seekg(0, std::ios::end);
                std::streampos filesize = file_stream.tellg();
                file_stream.close();

#define THREAD_NUM 4
                v_section_.resize(THREAD_NUM);
                std::streampos sizeConsume = 0;
                std::streampos sizePerBloack = filesize / THREAD_NUM;
                std::vector<std::thread> v_thread;
                for (int i = 0; i < THREAD_NUM; ++i) {
                    std::streampos start = i * sizePerBloack;
                    if (i == THREAD_NUM - 1) {
                        sizePerBloack = filesize - sizeConsume;
                    }
                    sizeConsume += sizePerBloack;
                    v_thread.emplace_back(&FSession::doFileSection, this, i, start, sizePerBloack);
                }
                for (auto& thread : v_thread) {
                    thread.join();
                }

                for (auto &it : v_section_) {
                    upgrade_file_content_.append(it);
                    it = std::string();
                }

                upgrade_file_size_ = upgrade_file_content_.size();

                result["Method"] = "UpgradeReply";
                result["Filename"] = upgrade_file_name_;
                result["Checksum"] = upgrade_file_checksum_;
                response.reset(new jsonrpcpp::Response(*request, result)); 
            }
        } else if (request->id().int_id() == MESSAGE_TYPE_FILE) {
            if (request->method() == "FileTransfer") {
                int account = request->params().get("Account");
                std::string name = request->params().get("Name");
                int to_account = request->params().get("ToAccount");
                std::string filename = request->params().get("Filename");
                int filesize = request->params().get("Filesize");
                std::string checksum = request->params().get("Checksum");
                std::string content = request->params().get("Content");
                int status = request->params().get("Status");

                // std::cout << "Account: " << std::to_string(account) << std::endl;
                // std::cout << "ToAccount: " << std::to_string(to_account) << std::endl;
                // std::cout << "Filename: " << filename << std::endl;
                // std::cout << "Filesize: " << std::to_string(filesize) << std::endl;
                // std::cout << "Checksum: " << checksum << std::endl;
                // std::cout << "Content: " << content << std::endl;
                // std::cout << "Status: " << std::to_string(status) << std::endl;

                notification.reset(new jsonrpcpp::Notification("FileTransferNotification",
                    jsonrpcpp::Parameter("Account", request->params().get("Account"), "Name", request->params().get("Name"),
                                        "ToAccount", request->params().get("ToAccount"), "Status", request->params().get("Status"),
                                        "Filename", request->params().get("Filename"), "Filesize", request->params().get("Filesize"),
                                        "Checksum", request->params().get("Checksum"), "Content", request->params().get("Content"))));
            } else if (request->method() == "UpgradeFileTransfer") {
                int account = request->params().get("Account");
                // std::cout << "Account: " << std::to_string(account) << std::endl;

#define M_BLOCK_SIZE 3000000
                int blocksize = M_BLOCK_SIZE;
                std::string content;
                if (upgrade_file_size_ - data_consume_ < blocksize) {
                    blocksize = upgrade_file_size_ - data_consume_;
                    content = upgrade_file_content_.substr(0, blocksize);
                    upgrade_file_content_ = std::string();
                    data_consume_ += blocksize;
                } else {
                    content = upgrade_file_content_.substr(0, blocksize);
                    upgrade_file_content_.erase(0, blocksize);
                    data_consume_ += blocksize;
                }

                int process = (data_consume_ * 100) / upgrade_file_size_;

                std::string encrypt = CBASE64::Encode(content.data(), content.size());

                result["Method"] = "UpgradeProcessing";
                result["Account"] = account;
                result["Content"] = encrypt;
                result["Checksum"] = upgrade_file_checksum_;
                result["Length"] = content.size();
                result["Process"] = process;
                response.reset(new jsonrpcpp::Response(*request, result));
            }
        } else {
            std::cout << "NULL Process Request\n";
        }
    } catch (const std::exception& e) {
        std::cout << "FServer::onMessageReceived exception: " << e.what() << ", message: " << request->to_json().dump() << "\n";
        response.reset(new jsonrpcpp::InternalErrorException(e.what(), request->id()));
    }
}

std::string FSession::doMessageReceived(const std::string& message)
{
    jsonrpcpp::entity_ptr entity(nullptr);
    try {
        entity = jsonrpcpp::Parser::do_parse(message);
        if (!entity)
            return "";
    } catch (const jsonrpcpp::ParseErrorException& e) {
        std::cout << e.to_json().dump() << "\nCaused by:" << message << "\n";
        return e.to_json().dump();
    } catch (const std::exception& e) {
        std::cout << e.what() << "\nCaused by:" << message << "\n";
        return jsonrpcpp::ParseErrorException(e.what()).to_json().dump();
    }
    jsonrpcpp::entity_ptr response(nullptr);
    jsonrpcpp::notification_ptr notification(nullptr);
    if (entity->is_request()) {
        auto self(shared_from_this());
        jsonrpcpp::request_ptr request = std::dynamic_pointer_cast<jsonrpcpp::Request>(entity);
        processRequest(request, response, notification);
        if (notification) {
            if (notification->method() == "FileTransferNotification") {
                int to_account = notification->params().get("ToAccount");
                
                if (!(int)notification->params().get("ToAccount")) {
                    group_.Deliver(self, notification->to_json().dump());
                } else {
                    group_.Deliver(to_account, notification->to_json().dump());
                }
                //std::cout << notification->to_json().dump() << std::endl;
            }
        }
        if (response) {
            //std::cout << "Response: " << response->to_json().dump() << "\n";
            return response->to_json().dump();
        }
        return "";
    }
}

void FSession::Start()
{
    doRead();
}

void FSession::doRead()
{
    auto self(shared_from_this());
    const std::string delimiter = "\n";
    boost::asio::async_read_until(
        socket_, streambuf_, delimiter,
        boost::asio::bind_executor(strand_, [this, self, delimiter](const std::error_code& ec, std::size_t bytes_transferred) {
            if (ec) {
                std::cerr << "[File Server] Account " << std::to_string(self->account_) << " offline\n";
                group_.Leave(self);
                return;
            }
            std::string line{buffers_begin(streambuf_.data()), buffers_begin(streambuf_.data()) + bytes_transferred - delimiter.length()};
            if (!line.empty()) {
                if (line.back() == '\r') {
                    line.resize(line.size() - 1);
                }

                std::string decrypt = AESDecrypt(line, key_);
                std::string decoded = decrypt.substr(0, decrypt.find("}}") + 2);

                std::string response = doMessageReceived(decoded);
                if (!response.empty()) {
                    Deliver(response);
                }
            }
            streambuf_.consume(bytes_transferred);
            doRead();
        }));
}

void FSession::doWrite()
{
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(messages_.front().data(), messages_.front().length()),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                messages_.pop_front();
                if (!messages_.empty())
                    doWrite();
            } else {
                group_.Leave(shared_from_this());
            }
        });
}

void FSession::Deliver(const std::string& msg)
{
    bool write_in_progress = !messages_.empty();

    std::string encoded = AESEncrypt(msg, key_);
    messages_.push_back(encoded + "\r\n");
    if (!write_in_progress) {
        doWrite();
    }
}

FServer::FServer(boost::asio::io_context& io_context, const tcp::endpoint& endpoint)
    : io_context_(io_context), acceptor_(io_context, endpoint)
{
    doAccept();
}

void FServer::doAccept()
{
    acceptor_.async_accept(
        [this] (boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<FSession>(io_context_, std::move(socket), group_)->Start();
            } else {
                std::cout << "Accept error: " << ec.message() << "\n";
            }
            doAccept();
        });
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc < 2)
        {
            std::cerr << "Usage: ./fserver <Group port>\n";
            return 1;
        }

        boost::asio::io_context io_context;

        std::list<FServer> fservers;
        for (int i = 1; i < argc; ++i)
        {
            tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[i]));
            fservers.emplace_back(io_context, endpoint);
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
            io_context.stop();
        });

        io_context.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "\nException Occurred\n";
        std::cout << e.what();
    }
    std::cout << "\nWeFish File Server terminated.\n";
    return 0;
}


