#pragma once

#include <cstring>
#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "mongoose/mongoose.h"

struct AllowedNet
{
    bool is_ipv6;
    union
    {
        struct
        {
            uint32_t ip;
            uint32_t mask;
        } v4;
        struct
        {
            uint8_t ip[16];
            uint8_t mask[16];
        } v6;
    } data;
};

class HostMatcher
{
private:
    std::vector<AllowedNet> networks;
    mutable std::mutex mtx;

    void createV6Mask(uint8_t *mask, int prefix)
    {
        std::memset(mask, 0, 16);
        for (int i = 0; i < prefix; ++i)
        {
            mask[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    std::string trim(const std::string &s)
    {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, (last - first + 1));
    }

    bool addNetwork(const std::string &cidr)
    {
        std::string ipPart = cidr;
        int prefix = -1;

        size_t slashPos = cidr.find('/');
        if (slashPos != std::string::npos)
        {
            ipPart = cidr.substr(0, slashPos);
            try
            {
                prefix = std::stoi(cidr.substr(slashPos + 1));
            }
            catch (...)
            {
                prefix = -1;
            }
        }

        AllowedNet entry;
        if (ipPart.find(':') != std::string::npos)
        {
            // IPv6
            entry.is_ipv6 = true;
            if (prefix < 0 || prefix > 128)
                prefix = 128;
            if (inet_pton(AF_INET6, ipPart.c_str(), entry.data.v6.ip) > 0)
            {
                createV6Mask(entry.data.v6.mask, prefix);
                networks.push_back(entry);
                return true;
            }
        }
        else
        {
            // IPv4
            entry.is_ipv6 = false;
            if (prefix < 0 || prefix > 32)
                prefix = 32;
            struct in_addr addr;
            if (inet_aton(ipPart.c_str(), &addr) != 0)
            {
                uint32_t m = (prefix == 0) ? 0 : htonl(~((1U << (32 - prefix)) - 1));
                entry.data.v4.ip = addr.s_addr & m;
                entry.data.v4.mask = m;
                networks.push_back(entry);
                return true;
            }
        }
        return false;
    }

public:
    bool loadFromFile(const std::string &filename)
    {
        std::lock_guard<std::mutex> lock(mtx);

        std::ifstream file(filename);
        if (!file.is_open())
        {
            isyslog("websocket: Error opening allowed_hosts file '%s' - %m", filename.c_str());
            networks.clear();
            return false;
        }

        networks.clear();
        std::string line;
        int lineCount = 0;
        int validEntries = 0;

        while (std::getline(file, line))
        {
            lineCount++;

            // Kommentare und Whitespace
            size_t commentPos = line.find('#');
            if (commentPos != std::string::npos)
                line.erase(commentPos);
            line = trim(line);

            if (line.empty())
                continue;

            if (addNetwork(line))
            { // addNetwork sollte bool zurückgeben
                validEntries++;
            }
            else
            {
                isyslog("websocket: Invalid host/network in %s line %d: '%s'",
                        filename.c_str(), lineCount, line.c_str());
            }
        }

        if (validEntries == 0)
        {
            isyslog("websocket: Warning: %s contains no valid entries - all connections will be rejected!", filename.c_str());
        }
        else
        {
            Debug("websocket: Loaded %d allowed host(s)/network(s) from %s",
                  validEntries, filename.c_str());
        }

        return (validEntries > 0);
    }

    bool isAllowed(const mg_addr &addr) const
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (networks.empty())
            return false;

        for (const auto &net : networks)
        {
            if (!addr.is_ip6)
            {
                // Client ist IPv4
                if (!net.is_ipv6)
                {
                    if ((addr.addr.ip4 & net.data.v4.mask) == net.data.v4.ip)
                        return true;
                }
            }
            else
            {
                // Client ist IPv6
                if (net.is_ipv6)
                {
                    bool match = true;
                    for (int i = 0; i < 16; ++i)
                    {
                        if ((addr.addr.ip[i] & net.data.v6.mask[i]) != net.data.v6.ip[i])
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                        return true;
                }
                else
                {
                    // Check IPv4-mapped-IPv6
                    const uint8_t *ip = addr.addr.ip;
                    if (IN6_IS_ADDR_V4MAPPED((struct in6_addr *)ip))
                    {
                        uint32_t clientV4;
                        std::memcpy(&clientV4, &ip[12], 4);
                        if ((clientV4 & net.data.v4.mask) == net.data.v4.ip)
                            return true;
                    }
                }
            }
        }
        return false;
    }
};
