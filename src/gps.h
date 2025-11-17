// This is a custom header for the GTK 3339
// also known as the adafruit ultimate GPS
// written by Sebastian Arthur

#ifndef _GPS_H
#define _GPS_H

typedef volatile char* message; 

typedef struct {
    message restart_type;
} restart_message_t;

static const restart_message_t COLD_RESTART = {
    .cold_restart = "$PMTK104*37<CR><LF>"
};


#endif // _GPS_H