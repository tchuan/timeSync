#ifndef _TIMESYNC_NTP_SUPPORT
#define _TIMESYNC_NTP_SUPPORT

// NTP References
struct NTP_Packet 
{
    int     Control_Word;
    int 	root_delay;
    int 	root_dispersion;
    int 	reference_identifier;
    __int64 reference_timestamp;
    __int64 originate_timestamp;
    __int64 receive_timestamp;
    int 	transmit_timestamp_seconds;
    int 	transmit_timestamp_fractions;

    NTP_Packet()
    {
        Control_Word   			     = htonl(0x0B000000);
        root_delay        			 = 0;
        root_dispersion  			 = 0;
        reference_identifier    	 = 0;
        reference_timestamp    	     = 0;
        originate_timestamp    	     = 0;
        receive_timestamp        	 = 0;
        transmit_timestamp_seconds   = 0;
        transmit_timestamp_fractions = 0;
    }
};

char* g_NTPServers[] = {"3.cn.pool.ntp.org",
                        "1.asia.pool.ntp.org",
                        "3.asia.pool.ntp.org"};

#endif