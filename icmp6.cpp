
#include "EtherSia.h"
#include "packet_headers.h"


void EtherSia::icmp6_ns_reply()
{
    IPv6Packet *packet = getPacket();

    // Is the Neighbour Solicitation addressed to us?
    if (!is_our_address(&ICMP6_NS_HEADER->target)) {
        return;
    }

    prepareReply();

    // We should now send a neighbor advertisement back to where the neighbor solicication came from.
    packet->length = ntohs(ICMP6_HEADER_LEN + ICMP6_NA_HEADER_LEN);
    ICMP6_HEADER->type = ICMP6_TYPE_NA;
    ICMP6_NA_HEADER->flags = ICMP6_FLAG_S; // Solicited flag.
    ICMP6_NA_HEADER->reserved[0] = ICMP6_NA_HEADER->reserved[1] = ICMP6_NA_HEADER->reserved[2] = 0;
    ICMP6_NA_HEADER->option_type = ICMP6_OPTION_TARGET_LINK_ADDRESS;
    ICMP6_NA_HEADER->option_len = 1;  // Options length, 1 = 8 bytes.
    ICMP6_NA_HEADER->option_mac = enc_mac_addr;

    icmp6_packet_send();
}

void EtherSia::icmp6_echo_reply()
{
    IPv6Packet *packet = getPacket();

    Serial.print(F("Ping from "));
    packet->src.println();

    prepareReply();

    ICMP6_HEADER->type = ICMP6_TYPE_ECHO_REPLY;
    ICMP6_HEADER->code = 0;

    icmp6_packet_send();
}

void EtherSia::icmp6_send_ns(IPv6Address *target_addr)
{
    IPv6Packet *packet = getPacket();

    packet->init();
    packet->length = ntohs(ICMP6_HEADER_LEN + ICMP6_RS_HEADER_LEN);
    packet->proto = IP6_PROTO_ICMP6;
    packet->hopLimit = 255;
    packet->src.setZero();
    packet->dest.setSolicitedNodeMulticastAddress(target_addr);

    memset(ETHER_HEADER, 0, ETHER_HEADER_LEN);
    packet->etherSrc = enc_mac_addr;
    packet->etherDest.setIPv6Multicast(packet->dest);

    memset(ICMP6_NS_HEADER, 0, ICMP6_NS_HEADER_LEN);
    ICMP6_NS_HEADER->target = *target_addr;

    icmp6_packet_send();
}

void EtherSia::icmp6_send_rs()
{
    IPv6Packet *packet = getPacket();

    packet->init();
    packet->length = ntohs(ICMP6_HEADER_LEN + ICMP6_RS_HEADER_LEN);
    packet->proto = IP6_PROTO_ICMP6;
    packet->hopLimit = 255;
    packet->src = link_local_addr;
    packet->dest.setLinkLocalAllRouters();

    packet->etherSrc = enc_mac_addr;
    packet->etherDest.setIPv6Multicast(packet->dest);

    memset(ICMP6_RS_HEADER, 0, ICMP6_RS_HEADER_LEN);
    ICMP6_HEADER->type = ICMP6_TYPE_RS;
    ICMP6_RS_HEADER->option_type = ICMP6_OPTION_SOURCE_LINK_ADDRESS;
    ICMP6_RS_HEADER->option_len = 1;
    ICMP6_RS_HEADER->option_mac = enc_mac_addr;

    icmp6_packet_send();
}

void EtherSia::icmp6_packet_send()
{
    IPv6Packet *packet = getPacket();

    ICMP6_HEADER->checksum = 0;
    ICMP6_HEADER->checksum = htons(packet->calculateChecksum());

    send();
}

void EtherSia::icmp6_process_prefix(struct icmp6_prefix_information *pi, MACAddress *router_mac_ptr)
{
    // FIXME: Only if On-link AND stateless address configuration flags set

    // FIXME: Only if prefix-length is 64?

    this->global_addr = pi->prefix;
    this->global_addr.setEui64(&enc_mac_addr);
    Serial.print(F("Global IP="));
    this->global_addr.println();

    this->router_mac = *router_mac_ptr;
    Serial.print(F("Router MAC="));
    this->router_mac.println();
}

void EtherSia::icmp6_process_ra()
{
    IPv6Packet *packet = getPacket();
    int16_t remaining = ntohs(packet->length) - ICMP6_HEADER_LEN - ICMP6_RA_HEADER_LEN;
    uint8_t *ptr = (uint8_t*)packet + ICMP6_RA_HEADER_OFFSET + ICMP6_RA_HEADER_LEN;
    uint8_t *pi_ptr = NULL;
    uint8_t *router_mac_ptr = NULL;

    // FIXME: check destination?

    while(remaining > 0) {
        switch(ptr[0]) {
        case ICMP6_OPTION_SOURCE_LINK_ADDRESS:
            router_mac_ptr = &ptr[2];
            break;
        case ICMP6_OPTION_PREFIX_INFORMATION:
            pi_ptr = &ptr[2];
            break;
        case ICMP6_OPTION_MTU:
            // FIXME: do something with the MTU?
            break;
        }

        remaining -= (8 * ptr[1]);
        ptr += (8 * ptr[1]);
    }

    if (pi_ptr && router_mac_ptr) {
        icmp6_process_prefix(
            (struct icmp6_prefix_information*)pi_ptr,
            (MACAddress *)router_mac_ptr
        );
    }
}

void EtherSia::icmp6_process_packet(uint16_t len)
{
    if (!icmp6_verify_checksum()) {
        Serial.println(F("ICMP6 checksum error."));
        return;
    }

    switch(ICMP6_HEADER->type) {
    case ICMP6_TYPE_NS:
        icmp6_ns_reply();
        break;

    case ICMP6_TYPE_ECHO:
        icmp6_echo_reply();
        break;

    case ICMP6_TYPE_RS:
        // We aren't a router - ignore all Router Solicitations
        break;

    case ICMP6_TYPE_RA:
        icmp6_process_ra();
        break;

    default:
        Serial.print(F("Unknown ICMP6 type="));
        Serial.println(ICMP6_HEADER->type, DEC);
        break;
    }
}

uint8_t EtherSia::icmp6_verify_checksum()
{
    IPv6Packet *packet = getPacket();
    uint16_t packetChecksum = ntohs(ICMP6_HEADER->checksum);

    // Set field in packet to 0 before calculating the checksum
    ICMP6_HEADER->checksum = 0;

    // Does the calculated checksum equal the checksum in the packet?
    return packet->calculateChecksum() == packetChecksum;
}
