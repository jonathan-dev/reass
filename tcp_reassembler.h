/*
 * Copyright 2011 Hylke Vellinga
 */


#include "uint128.h"
#include "ip_address.h"
#include "free_list.h"
#include "timeout.h"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/functional/hash/hash.hpp>
#include <boost/intrusive/list.hpp>
#include <map>

class packet_t;
class packet_listener_t;
class layer_t;
class tcphdr;

struct seq_nr_t
{
	seq_nr_t() {}
	seq_nr_t(uint32_t val) : d_val(val) {}

	uint32_t d_val;
};

inline bool operator <(const seq_nr_t &l, const seq_nr_t &r)
{
	int32_t diff = l.d_val - r.d_val;
	return diff < 0;
}
std::ostream &operator <<(std::ostream &os, const seq_nr_t &s);


struct tcp_stream_t :
	public free_list_member_t<tcp_stream_t>,
	public boost::intrusive::list_base_hook<
		boost::intrusive::link_mode<
			boost::intrusive::auto_unlink>
	>
{
	tcp_stream_t(tcp_stream_t *&free_head);
	~tcp_stream_t();

	void release(); // destructor
protected: // called from tcp_reassembler_t
	friend class tcp_reassembler_t;

	void set_src_dst_from_packet(const packet_t *packet, bool swap); // constructor(1/2)
	void init(packet_listener_t *listener); // constructor(2/2), will not touch src/dst

	void set_partner(tcp_stream_t *partner);

	void add(packet_t *packet, const layer_t *tcplay);

	//timeval timeout() const;
	template<typename TO>
	void set_timeout(TO &to);

	static tcp_stream_t *no_partner() { return (tcp_stream_t*)-1; }
	static tcp_stream_t *partner_destroyed() { return (tcp_stream_t*)-2; }

public:
	void set_userdata(void *userdata) { d_userdata = userdata; }
	void *userdata() const { return d_userdata; }
	bool closed() const { return d_have_accepted_end; }

	tcp_stream_t *partner() const { assert(have_partner()); return d_partner; }
	bool have_partner() const { return d_partner && d_partner != no_partner() && d_partner != partner_destroyed(); }

	bool initiator() const { assert(d_direction == direction_initiator || d_direction == direction_responder); return d_direction == direction_initiator; }
	bool responder() const { return !initiator(); }

	void print(std::ostream &os) const;

protected: // internal
	void accept_packet(packet_t *p, const layer_t *tcplay);
	void find_relyable_startseq(const tcphdr &hdr);
	void check_delayed(bool force = false);
	void find_direction(packet_t *packet, const layer_t *tcplay);
	void flush();

	packet_listener_t *d_listener;

	ip_address_t d_src;
	ip_address_t d_dst;

	bool d_trust_seq;
	seq_nr_t d_next_seq;
	bool d_have_accepted_end;

	tcp_stream_t *d_partner;
	enum direction_t { direction_unknown, direction_initiator, direction_responder };
	direction_t d_direction;

	void *d_userdata;

	timeval d_highest_ts;

	typedef std::multimap<seq_nr_t, packet_t *> delayed_t;
	delayed_t d_delayed;

	friend struct tcp_stream_equal_addresses;
	friend struct tcp_stream_hash_addresses;
};

std::ostream &operator <<(std::ostream &, const tcp_stream_t &);

struct tcp_stream_equal_addresses
{
	bool operator()(const tcp_stream_t*l, const tcp_stream_t*r) const
	{
		return l->d_src == r->d_src && l->d_dst == r->d_dst;
	}
};

struct tcp_stream_hash_addresses
{
	std::size_t operator()(const tcp_stream_t*s) const
	{
		std::size_t r = hash_value(s->d_src);
		boost::hash_combine(r, s->d_dst);
		return r;
	}
};

struct tcp_reassembler_t : private free_list_container_t<tcp_stream_t>
{
	tcp_reassembler_t(packet_listener_t *listener);
	~tcp_reassembler_t();

	void process(packet_t *packet);

	void flush();
protected:
	packet_listener_t *d_listener;
	//timeval d_now;

	typedef boost::multi_index_container<
		tcp_stream_t *,
		boost::multi_index::indexed_by<
			boost::multi_index::hashed_unique<
				boost::multi_index::identity<tcp_stream_t *>,
				tcp_stream_hash_addresses,
				tcp_stream_equal_addresses
			>
			//,
			//boost::multi_index::ordered_non_unique<
				//boost::multi_index::const_mem_fun<tcp_stream_t, timeval, &tcp_stream_t::timeout>
			//>
		>
	> stream_set_t;

	stream_set_t d_streams;
	typedef timeouts_t<610, 10, tcp_stream_t> tcp_timeouts_t;
	tcp_timeouts_t d_timeouts;

	stream_set_t::iterator find_or_create_stream(packet_t *packet, const layer_t *tcplay);
	void check_timeouts(uint64_t now);
	void close_stream(tcp_stream_t *stream);
};

#if 0
inline timeval tcp_stream_t::timeout() const
{
	bool use_short = d_have_accepted_end;
	timeval r = d_highest_ts;
	if (have_partner())
	{
		use_short ||= d_partner->d_have_accepted_end;
		timeval o = d_partner->d_highest_ts;
		if (o > r)
			r = o;
	}
	r.tv_sec += (use_short ? 60 : 600);
	return r;
}
#endif
