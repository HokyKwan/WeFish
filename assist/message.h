/**
 * @file          message.h
 * @author        HokyKwan(hoky.guan@tymphany.com)
 * @brief         Request Message Class
 * @date          2024-04-12
 * 
 * @copyright (c) 2024 by Tymphany Ltd, All Rights Reserved.
 * 
 */
#ifndef MESSAGE_H
#define MESSAGE_H

#include <iostream>
#include <sstream>

class Request
{
public:
    enum class field {
        host,
        target,
        user_agent,
        authorization,
        content_type,
        content_length,
        accept_language,
        accept,
        connection,
        body
    };

    enum class verb {
        get,
        post
    };

    Request() {
        save_.clear();
        req_content_.clear();
    };
    ~Request() = default;

    Request(const Request& other) : version_(other.version_),
        method_(other.method_), target_(other.target_),
        host_(other.host_), body_(other.body_) {
        save_ << other.save_.str();
        req_content_ << other.req_content_.str();
    }

    Request& operator=(const Request& other) {
        if (this != &other) {
            version_ = other.version_;
            method_ = other.method_;
            target_ = other.target_;
            host_ = other.host_;
            body_ = other.body_;

            save_.str(other.save_.str());
            req_content_.str(other.req_content_.str());
        }
        return *this;
    }

    operator std::string () const {
        return req_content_.str();
    };

    friend std::ostream& operator<<(std::ostream& os, const Request& req) {
        os << req.req_content_.str();
        return os;
    };

    void Version(int ver) {
        switch (ver) {
        case 10:
            version_ = "HTTP/1.0";
            break;
        default:
        case 11:
            version_ = "HTTP/1.1";
            break;
        }
    };

    void Method(verb v) {
        switch (v) {
        default:
        case verb::get:
            method_ = "GET";
            break;
        case verb::post:
            method_ = "POST";
            break;
        }
    };

    void Target(std::string t) {
        target_ = t;
    };

    std::string Get(field f) {
        switch (f) {
        case field::host:
            return host_;
        case field::target:
            return target_;
        case field::body:
            return body_;
        default:
            break;
        }
    };

    void Set(field f, std::string var) {
        switch (f) {
        case field::host:
            save_ << "Host: " << var << "\r\n";
            host_ = var;
            break;
        case field::user_agent:
            save_ << "User-Agent: " << var << "\r\n";
            break;
        case field::authorization:
            save_ << "Authorization: " << var << "\r\n";
            break;
        case field::content_type:
            save_ << "Content-Type: " << var << "\r\n";
            break;
        case field::content_length:
            save_ << "Content-Length: " << var << "\r\n";
            break;
        case field::accept_language:
            save_ << "Accept-Language: " << var << "\r\n";
            break;
        case field::accept:
            save_ << "Accept: " << var << "\r\n";
            break;
        case field::connection:
            save_ << "Connection: " << var << "\r\n";
            break;
        default:
            break;
        }
    };

    std::string &Body(void) {
        return body_;
    };

    void Prepare(void) {
        req_content_ << method_ << " " << target_ << " " << version_ << "\r\n"; 
        req_content_ << save_.str();
        req_content_ << "\r\n";
        if (body_ != "") {
            req_content_ << body_;
        }
    };

private:
    std::stringstream save_;
    std::stringstream req_content_;

    std::string version_;
    std::string method_;
    std::string target_;
    std::string host_;
    std::string body_;
};


#endif