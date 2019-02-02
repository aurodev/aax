/*
 * Copyright (C) 2018-2019 by Erik Hofman.
 * Copyright (C) 2018-2019 by Adalin B.V.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *        this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ADALIN B.V. ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL ADALIN B.V. OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUTOF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of Adalin B.V.
 */


#include <fstream>
#include <iostream>

#include <limits.h>
#include <assert.h>
#include <xml.h>

#include <base/timer.h>
#include "midi.hpp"

#define ENABLE_CSV	0
#if ENABLE_CSV
# define PRINT_CSV(...)	printf(__VA_ARGS__)
# define CSV(...)	if(midi.get_initialize()) printf(__VA_ARGS__)
#else
# define PRINT_CSV(...)
# define CSV(...)
#endif

#define DISPLAY(...)	if(midi.get_initialize() && midi.get_verbose()) printf(__VA_ARGS__)
#define MESSAGE(...)	if(!midi.get_initialize() && midi.get_verbose()) printf(__VA_ARGS__)

#ifndef NDEBUG
# define LOG(...)	printf(__VA_ARGS__)
#else
# define LOG
#endif

using namespace aax;

void
MIDI::rewind()
{
    channels.clear();
    uSPP = 500000/PPQN;
}

void MIDI::finish(uint8_t n)
{
    if (n >= channels.size() || !channels[n]) return;
    if (!channels[n]->finished()) {
        channels[n]->finish();
    }
}

bool
MIDI::finished(uint8_t n)
{
    if (n >= channels.size() || !channels[n]) return true;
    return channels[n]->finished();
}

void
MIDI::set_gain(float g)
{
    aax::dsp dsp = AeonWave::get(AAX_VOLUME_FILTER);
    dsp.set(AAX_GAIN, g);
    AeonWave::set(dsp);
}

void
MIDI::set_balance(float b)
{
    Matrix64 m;
    m.rotate(1.57*b, 0.0, 1.0, 0.0);
    m.inverse();
    AeonWave::matrix(m);
}

/*
 * Create map of instrument banks and program numbers with their associated
 * file names from the XML files for a quick access during playback.
 */
void
MIDI::read_instruments()
{
    const char *filename, *type = "instrument";
    auto map = instruments;

    std::string iname = path;
    iname.append("/");
    iname.append(instr);

    filename = iname.c_str();
    for(unsigned int i=0; i<2; ++i)
    {
        void *xid = xmlOpen(filename);
        if (xid)
        {
            void *xaid = xmlNodeGet(xid, "aeonwave/midi");
            char file[64] = "";
            if (xaid)
            {
                unsigned int bnum = xmlNodeGetNum(xaid, "bank");
                void *xbid = xmlMarkId(xaid);
                for (unsigned int b=0; b<bnum; b++)
                {
                    if (xmlNodeGetPos(xaid, xbid, "bank", b) != 0)
                    {
                        long int bank_no = xmlAttributeGetInt(xbid, "n");
                        unsigned int inum = xmlNodeGetNum(xbid, type);
                        void *xiid = xmlMarkId(xbid);

                        std::map<uint8_t,std::string> bank;
                        for (unsigned int i=0; i<inum; i++)
                        {
                            if (xmlNodeGetPos(xbid, xiid, type, i) != 0)
                            {
                                long int n = xmlAttributeGetInt(xiid, "n");
                                unsigned int slen;

                                slen = xmlAttributeCopyString(xiid, "file", file, 64);
                                if (slen)
                                {
                                    file[slen] = 0;
                                    std::string inst(file);
                                    bank.insert({n,inst});
                                }
                            }
                        }
                        map.insert({bank_no,bank});
                        xmlFree(xiid);
                    }
                }
                xmlFree(xbid);
                xmlFree(xaid);
            }
            else {
                std::cerr << "aeonwave/midi not found in: " << filename << std::endl;
            }
            xmlClose(xid);
        }
        else {
            std::cerr << "Unable to open: " << filename << std::endl;
        }

        if (i == 0)
        {
            instruments = map;

            iname = path;
            iname.append("/");
            iname.append(drum);
            filename = iname.c_str();
            type = "drum";
            map = drums;
        }
        else {
            drums = map;
        }
    }
}

/*
 * For drum mapping the program_no is stored in the bank number of the map
 * and the key_no in the program number of the map.
 */
std::string
MIDI::get_drum(uint8_t program_no, uint8_t key_no)
{
    auto itb = drums.find(program_no);
    if (itb == drums.end() && program_no > 0)
    {
        if ((program_no & 0xF8) == program_no) program_no = 0;
        else program_no &= 0xF8;
        itb = drums.find(program_no);
        if (itb == drums.end())
        {
            program_no = 0;
            itb = drums.find(program_no);
        }
    }

    if (itb != drums.end())
    {
        do
        {
            auto bank = itb->second;
            auto iti = bank.find(key_no);
            if (iti != bank.end()) {
                return iti->second;
            }

            if (program_no > 0)
            {
                if ((program_no & 0xF8) == program_no) program_no = 0;
                else program_no &= 0xF8;
                itb = drums.find(program_no);
                if (itb == drums.end())
                {
                    program_no = 0;
                    itb = drums.find(program_no);
                }
            }
            else break;
        }
        while (program_no >= 0);
    }
    LOG("Drum not found\n");
    return empty_str;
}

std::string
MIDI::get_instrument(uint8_t bank_no, uint8_t program_no)
{
    auto itb = instruments.find(bank_no);
    if (itb == instruments.end() && bank_no > 0) {
        itb = instruments.find(0);
    }

    if (itb != instruments.end())
    {
        do
        {
            auto bank = itb->second;
            auto iti = bank.find(program_no);
            if (iti != bank.end()) {
                return iti->second;
            }

            if (bank_no > 0)
            {
                bank_no = 0;
                itb = instruments.find(bank_no);
            }
            else break;
        }
        while (bank_no >= 0);
    }
    LOG("Instrument not found\n");
    return empty_str;
}

MIDIChannel&
MIDI::new_channel(uint8_t channel_no, uint8_t bank_no, uint8_t program_no)
{
    auto it = channels.find(channel_no);
    if (it != channels.end())
    {
        if (it->second) AeonWave::remove(*it->second);
        channels.erase(it);
    }

    try {
        auto ret = channels.insert(
            { channel_no, new MIDIChannel(*this, path, instr, drum,
                                          channel_no, bank_no, program_no)
            } );
        it = ret.first;
        AeonWave::add(*it->second);
    } catch(const std::invalid_argument& e) {
        throw;
    }
    return *it->second;
}

MIDIChannel&
MIDI::channel(uint8_t channel_no)
{
    auto it = channels.find(channel_no);
    if (it == channels.end()) {
        return new_channel(channel_no, 0, 0);
    }
    return *it->second;
}

/**
 * Note Off messages are ignored on Rhythm Channels, with the exception of the
 * ORCHESTRA SET (specifically, Note number 88) and the SFX SET
 * (Note numbers 47-84).
 *
 * Some percussion timbres require a mutually exclusive Note On/Off assignment.
 * For example, when a Note On message for Note number 42 (Closed Hi Hat) is
 * received while Note number 46 (Open Hi Hat) is sounding, Note number 46 is
 * promptly muted and Note number 42 sounds.
 *
 * <Standard Set> (1)
 * Scratch Push(29)  | Scratch Pull(30)
 * Closed HH(42)     | Pedal HH(44)     | Open HH(46)
 * Short Whistle(71) | Long Whistle(72)
 * Short Guiro(73)   | Long Guiro(74)
 * Mute Cuica(78)    | Open Cuica(79)
 * Mute Triangle(80) | Open Triangle(81)
 * Mute Surdo(86)    | Open Surdo(87)
 *
 * <Analog Set> (26)
 * Analog CHH 1(42) | Analog C HH 2(44) | Analog OHH (46)
 *
 * <Orchestra Set> (49)
 * Closed HH 2(27) | Pedal HH (28) | Open HH 2 (29)
 *
 * <SFX Set> (57)
 * Scratch Push(41) | Scratch Pull (42)
 */
bool
MIDI::process(uint8_t channel_no, uint8_t message, uint8_t key, uint8_t velocity, bool omni)
{
    // Omni mode: Device responds to MIDI data regardless of channel
    if (message == MIDI_NOTE_ON && velocity) {
        channel(channel_no).play(key, velocity);
    }
    else
    {
        if (message == MIDI_NOTE_ON) {
            velocity = 64;
        }
        channel(channel_no).stop(key, velocity);
    }
    return true;
}


void
MIDIChannel::play(uint8_t key_no, uint8_t velocity)
{
    assert (velocity);

    auto it = name_map.begin();
    if (midi.channel(channel_no).is_drums())
    {
        it = name_map.find(key_no);
        if (it == name_map.end())
        {
            std::string name = midi.get_drum(program_no, key_no);
            if (!name.empty())
            {
                if (!midi.buffer_avail(name)) {
                    DISPLAY("Loading drum:  %3i bank: %3i, program: %3i: %s\n",
                             key_no, bank_no, program_no, name.c_str());
                }
                Buffer &buffer = midi.buffer(name, true);
                if (buffer)
                {
                    auto ret = name_map.insert({key_no,buffer});
                    it = ret.first;
                }
            }
        }
    }
    else
    {
        it = name_map.find(program_no);
        if (it == name_map.end())
        {
            std::string name = midi.get_instrument(bank_no, program_no);
            if (!name.empty())
            {
                if (!midi.buffer_avail(name)) {
                    DISPLAY("Loading instrument bank: %3i, program: %3i: %s\n",
                             bank_no, program_no, name.c_str());
                }
                Buffer &buffer = midi.buffer(name, true);
                if (buffer)
                {
                    auto ret = name_map.insert({program_no,buffer});
                    it = ret.first;
                }
            }
        }
    }

    if (!midi.get_initialize() & it != name_map.end())
    {
        if (midi.channel(channel_no).is_drums())
        {
            switch(program_no)
            {
            case 0:	// Standard Set
                switch(key_no)
                {
                case 29: // EXC7
                    Instrument::stop(30, 0);
                    break;
                case 30: // EXC7
                    Instrument::stop(29, 0);
                    break;
                case 42: // EXC1
                    Instrument::stop(44, 0);
                    Instrument::stop(46, 0);
                    break;
                case 44: // EXC1
                    Instrument::stop(42, 0);
                    Instrument::stop(46, 0);
                    break;
                case 46: // EXC1
                    Instrument::stop(42, 0);
                    Instrument::stop(44, 0);
                    break;
                case 71: // EXC2
                    Instrument::stop(72, 0);
                    break;
                case 72: // EXC2
                    Instrument::stop(71, 0);
                    break;
                case 73: // EXC3
                    Instrument::stop(74, 0);
                    break;
                case 74: // EXC3
                    Instrument::stop(73, 0);
                    break;
                case 78: // EXC4
                    Instrument::stop(79, 0);
                    break;
                case 79: // EXC4
                    Instrument::stop(78, 0);
                    break;
                case 80: // EXC5
                    Instrument::stop(81, 0);
                    break;
                case 81: // EXC5
                    Instrument::stop(80, 0);
                    break;
                case 86: // EXC6
                    Instrument::stop(87, 0);
                    break;
                case 87: // EXC6
                    Instrument::stop(86, 0);
                    break;
                default:
                    break;
                }
                break;
            case 26:	// Analog Set
                switch(key_no)
                {
                case 42: // EXC1
                    Instrument::stop(44, 0);
                    Instrument::stop(46, 0);
                    break;
                case 44: // EXC1
                    Instrument::stop(42, 0);
                    Instrument::stop(46, 0);
                    break;
                case 46: // EXC1
                    Instrument::stop(42, 0);
                    Instrument::stop(44, 0);
                    break;
                default:
                    break;
                }
                break;
            case 48:	// Orchestra Set
                switch(key_no)
                {
                case 27: // EXC1
                    Instrument::stop(28, 0);
                    Instrument::stop(29, 0);
                    break;
                case 28: // EXC1
                    Instrument::stop(27, 0);
                    Instrument::stop(29, 0);
                    break;
                case 29: // EXC1
                    Instrument::stop(27, 0);
                    Instrument::stop(28, 0);
                    break;
                case 42: // EXC1
                    Instrument::stop(44, 0);
                    Instrument::stop(46, 0);
                    break;
                case 44: // EXC1
                    Instrument::stop(42, 0);
                    Instrument::stop(46, 0);
                    break;
                case 46: // EXC1
                    Instrument::stop(42, 0);
                    Instrument::stop(44, 0);
                    break;
                default:
                    break;
                }
                break;
            case 57:	// SFX Set
                switch(key_no)
                {
                case 41: // EXC7
                    Instrument::stop(42, 0);
                    break;
                case 42: // EXC7
                    Instrument::stop(41, 0);
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }

        Instrument::play(key_no, velocity, it->second);
    } else {
//      throw(std::invalid_argument("Instrument file "+name+" not found"));
    }
}


uint32_t
MIDITrack::pull_message()
{
    uint32_t rv = 0;

    for (int i=0; i<4; ++i)
    {
        uint8_t byte = pull_byte();

        rv = (rv << 7) | (byte & 0x7f);
        if ((byte & 0x80) == 0) {
            break;
        }
    }

    return rv;
}


// https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2
bool
MIDITrack::registered_param(uint8_t channel, uint8_t controller, uint8_t value)
{
    uint16_t type = value;
    bool data = false;
    bool rv = true;

#if 0
 value = pull_byte();
 printf("\t1: %x %x %x %x ", 0xb0|channel, controller, type, value);
 uint8_t *p = (uint8_t*)*this;
 p += offset();
 for (int i=0; i<20; ++i) printf("%x ", p[i]);
 printf("\n");
 push_byte();
#endif

    if (type > MAX_REGISTERED_PARAM) {
        type = MAX_REGISTERED_PARAM;
    }

    switch(controller)
    {
    case MIDI_REGISTERED_PARAM_COARSE:
        msb_type = type;
        break;
    case MIDI_REGISTERED_PARAM_FINE:
        lsb_type = type;
        break;
    case MIDI_DATA_ENTRY:
        if (registered)
        {
            param[msb_type].coarse = value;
            data = true;
        }
        break;
    case MIDI_DATA_ENTRY|MIDI_FINE:
        if (registered)
        {
            param[lsb_type].fine = value;
            data = true;
        }
        break;
    case MIDI_DATA_INCREMENT:
        type = msb_type << 8 | lsb_type;
        if (++param[type].fine == 128) {
            param[type].coarse++;
            param[type].fine = 0;
        }
        break;
    case MIDI_DATA_DECREMENT:
        type = msb_type << 8 | lsb_type;
        if (param[type].fine == 0) {
            param[type].coarse--;
            param[type].fine = 127;
        } else {
            param[type].fine--;
        }
        break;
    case MIDI_UNREGISTERED_PARAM_FINE:
    case MIDI_UNREGISTERED_PARAM_COARSE:
        break;
    default:
        LOG("Unsupported registered parameter: %x\n", controller);
        rv = false;
        break;
    }

    if (data)
    {
        type = msb_type << 8 | lsb_type;
        switch(type)
        {
        case MIDI_PITCH_BEND_SENSITIVITY:
        {
            float val;
            val = (float)param[MIDI_PITCH_BEND_SENSITIVITY].coarse +
                  (float)param[MIDI_PITCH_BEND_SENSITIVITY].fine*0.01f;
            midi.channel(channel).set_semi_tones(val);
            break;
        }
        case MIDI_MODULATION_DEPTH_RANGE:
        {
            float val;
            val = (float)param[MIDI_MODULATION_DEPTH_RANGE].coarse +
                  (float)param[MIDI_MODULATION_DEPTH_RANGE].fine*0.01f;
            midi.channel(channel).set_modulation_depth(val);
            break;
        }
        case MIDI_PARAMETER_RESET:
            midi.channel(channel).set_semi_tones(2.0f);
            break;
        case MIDI_CHANNEL_FINE_TUNING:
        case MIDI_CHANNEL_COARSE_TUNING:
            break;
        case MIDI_TUNING_PROGRAM_CHANGE:
        case MIDI_TUNING_BANK_SELECT:
            break;
        default:
            LOG("Unsupported registered parameter: 0x%x\n", type);
            break;
        }
    }

#if 0
 printf("\t9: ");
 p = (uint8_t*)*this;
 p += offset();
 for (int i=0; i<20; ++i) printf("%x ", p[i]);
 printf("\n");
#endif

    return rv;
}

void
MIDITrack::rewind()
{
    byte_stream::rewind();
    timestamp_parts = pull_message()*24/600000;

    program_no = 0;
    bank_no = 0;
    previous = 0;
    polyphony = true;
    omni = true;
}

bool
MIDITrack::process(uint64_t time_offs_parts, uint32_t& elapsed_parts, uint32_t& next)
{
    bool rv = !eof();

    if (eof())
    {
        if (midi.get_format() && !channel_no) return rv;
        return !midi.finished(channel_no);
    }

    if (elapsed_parts < wait_parts)
    {
        wait_parts -= elapsed_parts;
        next = wait_parts;
        return rv;
    }

    while (!eof() && (timestamp_parts <= time_offs_parts))
    {
        uint32_t message = pull_byte();

        CSV("%d, %ld, ", channel_no+1, timestamp_parts);

        // Handle running status; if the next byte is a data byte
        // reuse the last command seen in the track
        if ((message & 0x80) == 0)
        {
            push_byte();
            message = previous;
        }
        else if ((message & 0xF0) != 0xF0)
        {
            // System messages and file meta-events (all of which are in the
            // 0xF0-0xFF range) are not saved, as it is possible to carry a
            // running status across them.
            previous = message;
        }

        rv = true;
        switch(message)
        {
        case MIDI_SYSTEM_EXCLUSIVE:
        {
            uint8_t size = pull_byte();
            uint8_t byte = pull_byte();
            const char *s = NULL;
#if 0
 uint8_t *p = (uint8_t*)*this;
 p += offset();
 for (int i=0; i<20; ++i) printf("%d, ", p[i]);
 printf("\n");
#endif

            CSV("System_exclusive, %u, %d", size, byte);
            switch(byte)
            {
            case MIDI_SYSTEM_EXCLUSIVE_ROLAND:
                if (pull_byte() == 0x10 && pull_byte() == 0x42 &&
                    pull_byte() == 0x12 && pull_byte() == 0x40 &&
                    pull_byte() == 0x00 && pull_byte() == 0x7f &&
                    pull_byte() == 0x00 && pull_byte() == 0x41)
                {
                    midi.set_mode(MIDI_GENERAL_STANDARD);
                    CSV(", %d, %d, %d, %d, %d, %d, %d, %d, %d",
                         0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41);
                }
                break;
            case MIDI_SYSTEM_EXCLUSIVE_YAMAHA:
                byte = pull_byte();
                if ((byte & 0x10) && pull_byte() == 0x4c &&
                    pull_byte() == 0x00 && pull_byte() == 0x00 &&
                    pull_byte() == 0x7e && pull_byte() == 0x00)
                {
                    midi.set_mode(MIDI_XG_MIDI);
                    CSV(", %d, %d, %d, %d, %d, %d, %d, %d, %d",
                         0x43, byte, 0x00, 0x00, 0x7E, 0x00);
                }
                break;
            case MIDI_SYSTEM_EXCLUSIVE_NON_REALTIME:
                // GM1 rewind: F0 7E 7F 09 01 F7
                // GM2 rewind: F0 7E 7F 09 03 F7
                // GS  rewind: F0 41 10 42 12 40 00 7F 00 41 F7
                byte = pull_byte();         // device id.
                if (byte == 0x7F)
                {
                    byte = pull_byte();
                    switch(byte)
                    {
                    case GENERAL_MIDI_SYSTEM:
                        byte = pull_byte();
                        midi.set_mode(byte);
                        switch(byte)
                        {
                        case 0x01:
                            midi.set_mode(MIDI_GENERAL_MIDI1);
                            break;
                        case 0x02:
                            midi.set_mode(MIDI_MODE0);
                            break;
                        case 0x03:
                            midi.set_mode(MIDI_GENERAL_MIDI2);
                            break;
                        default:
                            break;
                        }
                        CSV(", %d, %d, %d, %d", 0x7E, 0x7F, 0x09, byte);
                        break;
                    case MIDI_EOF:
                    case MIDI_WAIT:
                    case MIDI_CANCEL:
                    case MIDI_NAK:
                    case MIDI_ACK:
                        break;
                    default:
                        break;
                    }
                }
                break;
            case MIDI_SYSTEM_EXCLUSIVE_REALTIME:
                byte = pull_byte();
                switch(byte)
                {
                case MIDI_DEVICE_CONTROL:
                    byte = pull_byte();
                    switch(byte)
                    {
                    case MIDI_DEVICE_MASTER_VOLUME:
                        byte = pull_byte();
                        if (midi.get_mode() == MIDI_GENERAL_MIDI2) {
                            midi.set_gain((float)byte/127.0f);
                        }
                        break;
                    case MIDI_DEVICE_MASTER_BALANCE:
                        byte = pull_byte();
                        if (midi.get_mode() == MIDI_GENERAL_MIDI2) {
                            midi.set_balance(((float)byte-64.0f)/64.0f);
                        }
                        break;
                    default:
                        LOG("Unsupported sysex parameter: %x\n", byte);
                        byte = pull_byte();
                        break;
                    }
                    break;
                default:
                    break;
                }
            case MIDI_SYSTEM_EXCLUSIVE_END:
            default:
                break;
            }

            do {
                byte = pull_byte();
                CSV(", %d", byte);
            } while (byte != MIDI_SYSTEM_EXCLUSIVE_END);
            CSV("\n");
            break;
        }
        case MIDI_FILE_META_EVENT:
        {
            uint8_t meta = pull_byte();
            uint8_t size = pull_byte();
            uint8_t c;
            switch(meta)
            {
            case MIDI_TEXT:
            case MIDI_COPYRIGHT:
            case MIDI_TRACK_NAME:
            case MIDI_INSTRUMENT_NAME:
                CSV("%s, \"", csv_name[meta-1].c_str());
                MESSAGE("%-10s: ", type_name[meta-1].c_str());
                for (int i=0; i<size; ++i)
                {
                    c = pull_byte();
                    MESSAGE("%c", c);
                    CSV("%c", c);
                }
                MESSAGE("\n");
                CSV("\"\n");
                break;
            case MIDI_LYRICS:
                midi.set_lyrics(true);
                CSV("%s, ", csv_name[meta-1].c_str());
                for (int i=0; i<size; ++i)
                {
                    c = pull_byte();
                    MESSAGE("%c", c);
                    CSV("%c", c);
                }
                if (!midi.get_initialize() && midi.get_verbose()) fflush(stdout);
                CSV("\n");
                break;
            case MIDI_MARKER:
                CSV("Marker_t, ");
                for (int i=0; i<size; ++i) {
                    uint8_t c = pull_byte();
                    CSV("%c", c);
                }
                CSV("\n");
                break;
            case MIDI_CUE_POINT:
                CSV("Cue_point_t, ");
                for (int i=0; i<size; ++i) {
                    uint8_t c = pull_byte();
                    CSV("%c", c);
                }
                CSV("\n");
            case MIDI_DEVICE_NAME:
                CSV("Device_name_t, ");
                for (int i=0; i<size; ++i) {
                    uint8_t c = pull_byte();
                    CSV("%c", c);
                }
                CSV("\n");
                break;
            case MIDI_CHANNEL_PREFIX:
                c = pull_byte();
                channel_no = (channel_no & 0xF0) | c;
                CSV("Channel_prefix, %d\n", c);
                break;
            case MIDI_PORT_PREFERENCE:
                c = pull_byte();
                channel_no = (channel_no & 0xF) | (c << 8);
                CSV("MIDI_port, %d\n", c);
                break;
            case MIDI_END_OF_TRACK:
                CSV("End_track\n");
                forward();
                break;
            case MIDI_SET_TEMPO:
            {
                uint32_t tempo;
                tempo = (pull_byte() << 16) | (pull_byte() << 8) | pull_byte();
                midi.set_tempo(tempo);
                CSV("Tempo, %d\n", tempo);
                break;
            }
            case MIDI_SEQUENCE_NUMBER:        // sequencer software only
            {
                uint8_t mm = pull_byte();
                uint8_t ll = pull_byte();
                CSV("Sequence_number, %d\n", (mm << 8) | ll);
                forward(size);
                break;
            }
            case MIDI_TIME_SIGNATURE:
            {
                uint8_t nn = pull_byte();
                uint8_t dd = pull_byte();
                uint8_t cc = pull_byte(); // 1 << cc
                uint8_t bb = pull_byte();
                uint16_t QN = 100000.0f / (float)cc;
                CSV("Time_signature, %d, %d, %d, %d\n", nn, dd, cc, bb);

                break;
            }
            case MIDI_SMPTE_OFFSET:
            {
                uint8_t hr = pull_byte();
                uint8_t mn = pull_byte();
                uint8_t se = pull_byte();
                uint8_t fr = pull_byte();
                uint8_t ff = pull_byte();
                CSV( "SMPTE_offset, %d, %d, %d, %d, %d\n", hr, mn, se, fr, ff);
                break;
            }
            case MIDI_KEY_SIGNATURE:
            {
                uint8_t sf = pull_byte();
                uint8_t mi = pull_byte();
                CSV("Key_signature, %d, \"%s\"\n", sf, mi ? "minor" : "major");
                break;
            }
            case MIDI_SEQUENCERSPECIFICMETAEVENT:
                CSV("Sequencer_specific, %u", size);
                for (int i=0; i<size; ++i) {
                    uint8_t c = pull_byte();
                    CSV("%c", c);
                }
                CSV("\n");
                break;
            default:        // unsupported
                LOG("Unsupported system message: 0x%x\n", meta);
                forward(size);
                break;
            }
        }
        default:
        {
            uint8_t channel = message & 0xf;
            switch(message & 0xf0)
            {
            case MIDI_NOTE_OFF:
            {
                int16_t key = pull_byte();
                uint8_t velocity = pull_byte();
                if (!midi.channel(channel).is_drums()) {
                    key = (key-0x20) + param[MIDI_CHANNEL_COARSE_TUNING].coarse;
                }
                midi.process(channel, message & 0xf0, key, velocity, omni);
                CSV("Note_off_c, %d, %d, %d\n", channel, key, velocity);
                break;
            }
            case MIDI_NOTE_ON:
            {
                int16_t key = pull_byte();
                uint8_t velocity = pull_byte();
                if (!midi.channel(channel).is_drums()) {
                    key = (key-0x20) + param[MIDI_CHANNEL_COARSE_TUNING].coarse;
                }
                midi.process(channel, message & 0xf0, key, velocity, omni);
                CSV("Note_on_c, %d, %d, %d\n", channel, key, velocity);
                break;
            }
            case MIDI_POLYPHONIC_AFTERTOUCH:
            {
                uint8_t key = pull_byte();
                uint8_t pressure = pull_byte();
                midi.channel(channel).set_pitch(key, pitch2cents((float)pressure/127.0f, channel));
                midi.channel(channel).set_pressure(key, 1.0f-0.33f*pressure/127.0f);
                CSV("Poly_aftertouch_c, %d, %d, %d\n", channel, key, pressure);
                break;
            }
            case MIDI_CHANNEL_AFTERTOUCH:
            {
                uint8_t pressure = pull_byte();
                midi.channel(channel).set_pitch(pitch2cents((float)pressure/127.0f, channel));
                midi.channel(channel).set_pressure(1.0f-0.33f*pressure/127.0f);
                CSV("Channel_aftertouch_c, %d, %d\n", channel, pressure);
                break;
            }
            case MIDI_BREATH_CONTROLLER:
            {
                uint8_t pressure = pull_byte();
                midi.channel(channel).set_pressure(1.0f-0.33f*pressure/127.0f);
                break;
            }
            case MIDI_CONTROL_CHANGE:
            {
                // http://midi.teragonaudio.com/tech/midispec/ctllist.htm
                uint8_t controller = pull_byte();
                uint8_t value = pull_byte();
                CSV("Control_c, %d, %d, %d\n", channel, controller, value);
                switch(controller)
                {
                case MIDI_ALL_CONTROLLERS_OFF:
                    midi.channel(channel).set_modulation(0.0f);
                    midi.channel(channel).set_expression(1.0f);
                    midi.channel(channel).set_hold(false);
//                  midi.channel(channel).set_portamento(false);
                    midi.channel(channel).set_sustain(false);
                    midi.channel(channel).set_soft(false);
                    midi.channel(channel).set_semi_tones(2.0f);
                    midi.channel(channel).set_pitch(1.0f);
                    msb_type = lsb_type = 0x7F;
                    // midi.channel(channel).set_gain(100.0f/127.0f);
                    // midi.channel(channel).set_pan(0.0f);
                    // intentional falltrough
                case MIDI_MONO_ALL_NOTES_OFF:
                    midi.process(channel, MIDI_NOTE_OFF, 0, 0, true);
                    mode = MIDI_MONOPHONIC;
                    break;
                case MIDI_POLY_ALL_NOTES_OFF:
                    midi.process(channel, MIDI_NOTE_OFF, 0, 0, true);
                    mode = MIDI_POLYPHONIC;
                    break;
                case MIDI_ALL_SOUND_OFF:
                    midi.process(channel, MIDI_NOTE_OFF, 0, 0, true);
                    break;
                case MIDI_OMNI_OFF:
                    midi.process(channel, MIDI_NOTE_OFF, 0, 0, true);
                    omni = false;
                    break;
                case MIDI_OMNI_ON:
                    midi.process(channel, MIDI_NOTE_OFF, 0, 0, true);
                    omni = true;
                    break;
                case MIDI_BANK_SELECT:
                    bank_no = value * 128;
                    break;
                case MIDI_BANK_SELECT|MIDI_FINE:
                    bank_no += value;
                    break;
                case MIDI_BALANCE:
                    // If a MultiTimbral device, then each Part usually has its
                    // own Balance. This is generally when Balance becomes
                    // useful, because then you can use Pan, Volume, and Balance
                    // controllers to internally mix all of the Parts to the
                    // device's stereo outputs
                    LOG("Unsupported control change: MIDI_BALANCE, ch: %u, value: %u\n", channel, value);
                    break;
                case MIDI_PAN:
                    midi.channel(channel).set_pan(((float)value-64.0f)/64.0f);
                    break;
                case MIDI_EXPRESSION:
                    midi.channel(channel).set_expression((float)value/127.0f);
                    break;
                case MIDI_MODULATION_DEPTH:
                {
                    float depth = (float)(value << 7)/16383.0f;
                    depth = modulation2cents(depth, channel) - 1.0f;
                    midi.channel(channel).set_modulation(depth);
                    break;
                }
                case MIDI_CELESTE_EFFECT_DEPTH:
                {
                    float level = (float)value/127.0f;
                    level = pitch2cents(level, channel);
                    midi.channel(channel).set_detune(level);
                    break;
                }
                case MIDI_CHANNEL_VOLUME:
                    midi.channel(channel).set_gain((float)value/127.0f);
                    break;
                case MIDI_ALL_NOTES_OFF:
                    for(auto& it : midi.channel())
                    {
                        midi.process(it.first, MIDI_NOTE_OFF, 0, 0, true);
                        midi.channel(channel).set_semi_tones(2.0f);
                    }
                    break;
                case MIDI_UNREGISTERED_PARAM_COARSE:
                case MIDI_UNREGISTERED_PARAM_FINE:
                    registered = false;
                    registered_param(channel, controller, value);
                    break;
                case MIDI_REGISTERED_PARAM_COARSE:
                case MIDI_REGISTERED_PARAM_FINE:
                    registered = true;
                    registered_param(channel, controller, value);
                    break;
                case MIDI_DATA_ENTRY:
                case MIDI_DATA_ENTRY|MIDI_FINE:
                case MIDI_DATA_INCREMENT:
                case MIDI_DATA_DECREMENT:
                    registered_param(channel, controller, value);
                    break;
                case MIDI_SOFT_PEDAL_SWITCH:
                    midi.channel(channel).set_soft(value >= 0x40);
                    break;
                case MIDI_LEGATO_SWITCH:
                    LOG("Unsupported control change: MIDI_LEGATO_SWITCH, ch: %u, value: %u\n", channel, value);
                    break;
                case MIDI_DAMPER_PEDAL_SWITCH:
                    midi.channel(channel).set_hold(value >= 0x40);
                    break;
                case MIDI_SOSTENUTO_SWITCH:
                    midi.channel(channel).set_sustain(value >= 0x40);
                    break;
                case MIDI_REVERB_SEND_LEVEL:
                    if (midi.get_mode() >= MIDI_GENERAL_MIDI2) {
                        float val = (float)value/127.0f;
                        midi.channel(channel).set_reverb_level(val);
                    }
                    break;
                case MIDI_CHORUS_SEND_LEVEL:
                    if (midi.get_mode() >= MIDI_GENERAL_MIDI2) {
                        float val = (float)value/127.0f;
                        midi.channel(channel).set_chorus_level(val);
                    }
                    break;
                case MIDI_FILTER_RESONANCE:
                    if (midi.get_mode() >= MIDI_GENERAL_MIDI2) {
                        float val = (float)value/64.0f;
                        midi.channel(channel).set_filter_resonance(val);
                    }
                    break;
                case MIDI_CUTOFF:
                    if (midi.get_mode() >= MIDI_GENERAL_MIDI2) {
                        float val = (float)value/64.0f;
                        midi.channel(channel).set_filter_cutoff(val);
                    }
                    break;
                case MIDI_VIBRATO_RATE:
                    if (midi.get_mode() >= MIDI_GENERAL_MIDI2) {
                        float val = 0.5f + (float)value/64.0f;
                        midi.channel(channel).set_vibrato_rate(val);
                    }
                    break;
                case MIDI_VIBRATO_DEPTH:
                    if (midi.get_mode() >= MIDI_GENERAL_MIDI2) {
                        float val = (float)value/64.0f;
                        midi.channel(channel).set_vibrato_depth(val);
                    }
                    break;
                case MIDI_VIBRATO_DELAY:
                    if (midi.get_mode() >= MIDI_GENERAL_MIDI2) {
                        float val = (float)value/64.0f;
                        midi.channel(channel).set_vibrato_delay(val);
                    }
                    break;
                case MIDI_PORTAMENTO_TIME:
                    LOG("Unsupported control change: MIDI_PORTAMENTO_TIME, ch: %u, value: %u\n", channel, value);
                    break;
                case MIDI_PORTAMENTO_SWITCH:            // GM 2.0
                    LOG("Unsupported control change: MIDI_PORTAMENTO_SWITCH, ch: %u, value: %u\n", channel, value);
                    break;
                case MIDI_RELEASE_TIME:			// GM 2.0
                    LOG("Unsupported control change: MIDI_RELEASE_TIME, ch: %u, value: %u\n", channel, value);
                    break;
                case MIDI_ATTACK_TIME:			// GM 2.0
                    LOG("Unsupported control change: MIDI_ATTACK_TIME, ch: %u, value: %u\n", channel, value);
                    break;
                case MIDI_DECAY_TIME:			// GM 2.0
                    LOG("Unsupported control change: MIDI_DECAY_TIME, ch: %u, value: %u\n", channel, value);
                    break;
                case MIDI_TREMOLO_EFFECT_DEPTH:
//                  midi.channel(channel).set_tremolo_depth((float)value/64.0f);
                    break;
                case MIDI_PHASER_EFFECT_DEPTH:
//                  midi.channel(channel).set_phaser_depth((float)value/64.0f);
                    break;
                case MIDI_PORTAMENTO_CONTROL:
                case MIDI_HOLD2:
                case MIDI_PAN|MIDI_FINE:
                case MIDI_EXPRESSION|MIDI_FINE:
                case MIDI_BREATH_CONTROLLER|MIDI_FINE:
                case MIDI_BALANCE|MIDI_FINE:
                case MIDI_SOUND_VARIATION:
                case MIDI_SOUND_CONTROL10:
                case MIDI_HIGHRES_VELOCITY_PREFIX:
                case MIDI_GENERAL_PURPOSE_CONTROL1:
                case MIDI_GENERAL_PURPOSE_CONTROL2:
                case MIDI_GENERAL_PURPOSE_CONTROL3:
                case MIDI_GENERAL_PURPOSE_CONTROL4:
                case MIDI_GENERAL_PURPOSE_CONTROL5:
                case MIDI_GENERAL_PURPOSE_CONTROL6:
                case MIDI_GENERAL_PURPOSE_CONTROL7:
                case MIDI_GENERAL_PURPOSE_CONTROL8:
                    LOG("Unsupported control change: %x, ch: %u, value: %u\n", controller ,channel, value);
                    break;
                default:
                    LOG("Unsupported control change: 0x%x\n", controller);
                    break;
                }
                break;
            }
            case MIDI_PROGRAM_CHANGE:
            {
                uint8_t program_no = pull_byte();
                CSV("Program_c, %d, %d\n", channel, program_no);
                try {
                    midi.new_channel(channel, bank_no, program_no);
                } catch(const std::invalid_argument& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                }
                break;
            }
            case MIDI_PITCH_BEND:
            {
                int16_t pitch = pull_byte() | pull_byte() << 7;
                float pitch_bend = (float)pitch-8192.0f;
                if (pitch_bend < 0) pitch_bend /= 8192.0f;
                else pitch_bend /= 8191.0f;
                pitch_bend = pitch2cents(pitch_bend, channel);
                midi.channel(channel).set_pitch(pitch_bend);
                CSV("Pitch_bend_c, %d, %d\n", channel, pitch);
                break;
            }
            case MIDI_SYSTEM:
                switch(channel)
                {
                case MIDI_TIMING_CODE:
                    pull_byte();
                    break;
                case MIDI_POSITION_POINTER:
                    pull_byte();
                    pull_byte();
                    break;
                case MIDI_SONG_SELECT:
                    pull_byte();
                    break;
                case MIDI_TUNE_REQUEST:
                    break;
                case MIDI_SYSTEM_RESET:
#if 0
                    omni = true;
                    polyphony = true;
                    for(auto& it : midi.channel())
                    {
                        midi.process(it.first, MIDI_NOTE_OFF, 0, 0, true);
                        midi.channel(channel).set_semi_tones(2.0f);
                    }
#endif
                    break;
                case MIDI_TIMING_CLOCK:
                case MIDI_START:
                case MIDI_CONTINUE:
                case MIDI_STOP:
                case MIDI_ACTIVE_SENSE:
                    break;
                default:
                    LOG("Unsupported real-time System message: 0x%x - %d\n", message, channel);
                    break;
                }
                break;
            default:
                LOG("Unsupported message: 0x%x\n", message);
                break;
            }
            break;
        } // switch
        } // default

        if (!eof())
        {
            wait_parts = pull_message();
            timestamp_parts += wait_parts;
        }
    }
    next = wait_parts;

    return rv;
}


MIDIFile::MIDIFile(const char *devname, const char *filename) : MIDI(devname)
{
    std::ifstream file(filename, std::ios::in|std::ios::binary|std::ios::ate);
    ssize_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > 0)
    {
        midi_data.reserve(size);
        if (midi_data.capacity() == size)
        {
            std::streamsize fileSize = size;
            if (file.read((char*)midi_data.data(), fileSize))
            {
                buffer_map<uint8_t> map(midi_data.data(), size);
                byte_stream stream(map);

                try
                {
                    uint32_t size, header = stream.pull_long();
                    uint16_t format, track_no = 0;

                    if (header == 0x4d546864) // "MThd"
                    {
                        size = stream.pull_long();
                        if (size != 6) return;

                        format = stream.pull_word();
                        if (format != 0 && format != 1) return;

                        no_tracks = stream.pull_word();
                        if (format == 0 && no_tracks != 1) return;

                        midi.set_format(format);

                        uint16_t PPQN = stream.pull_word();
                        if (PPQN & 0x8000) // SMPTE
                        {
                            uint8_t fps = (PPQN >> 8) & 0xff;
                            uint8_t resolution = PPQN & 0xff;
                            if (fps == 232) fps = 24;
                            else if (fps == 231) fps = 25;
                            else if (fps == 227) fps = 29;
                            else if (fps == 226) fps = 30;
                            else fps = 0;
                            PPQN = fps*resolution;
                        }
                        midi.set_ppqn(PPQN);
                    }

                    while (!stream.eof())
                    {
                        header = stream.pull_long();
                        if (header == 0x4d54726b) // "MTrk"
                        {
                            uint32_t length = stream.pull_long();
                            track.push_back(new MIDITrack(*this, stream, length, track_no++));
                            stream.forward(length);
                            PRINT_CSV("%d, 0, Start_track\n", track_no);
                        }
                        else {
                            break;
                        }
                    }
                    no_tracks = track_no;

                } catch (const std::overflow_error& e) {
                    std::cerr << "Error while processing the MIDI file: "
                              << e.what() << std::endl;
                }
            }
            else {
                std::cerr << "Error: Unable to open: " << filename << std::endl;
            }
        }
        else if (!midi_data.size()) {
            std::cerr << "Error: Out of memory." << std::endl;
        }
    }
    else {
        std::cerr << "Error: Unable to open: " << filename << std::endl;
    }
}

void
MIDIFile::initialize()
{
    midi.read_instruments();

    midi.set_initialize(true);
    duration_sec = 0.0f;

    uint64_t time_parts = 0;
    uint32_t wait_parts = 1000000;
    while (process(time_parts, wait_parts))
    {
        time_parts += wait_parts;
        duration_sec += wait_parts*midi.get_uspp()*1e-6f;
    }

    midi.set_initialize(false);
    rewind();

    if (midi.get_verbose())
    {
        float hour, minutes, seconds;

        unsigned int format = midi.get_format();
        if (format >= MIDI_FILE_FORMAT_MAX) format = MIDI_FILE_FORMAT_MAX;
        MESSAGE("Format    : %s\n", format_name[format].c_str());

        unsigned int mode = midi.get_mode();
        assert(mode < MIDI_MODE_MAX);
        MESSAGE("MIDI Mode : %s\n", mode_name[mode].c_str());

        seconds = duration_sec;
        hour = floorf(seconds/(60.0f*60.0f));
        seconds -= hour*60.0f*60.0f;
        minutes = floorf(seconds/60.0f);
        seconds -= minutes*60.0f;
        if (hour) {
            MESSAGE("Duration  : %02.0f:%02.0f:%02.0f hours\n", hour, minutes, seconds);
        } else {
            MESSAGE("Duration  : %02.0f:%02.0f minutes\n", minutes, seconds);
        }
    }

    midi.set(AAX_REFRESH_RATE, 90.0f);
    midi.set(AAX_INITIALIZED);
    pos_sec = 0;
}

void
MIDIFile::rewind()
{
    midi.rewind();
    for (auto it : track) {
        it->rewind();
    }
}

bool
MIDIFile::process(uint64_t time_parts, uint32_t& next)
{
    uint32_t elapsed_parts = next;
    uint32_t wait_parts;
    bool rv = false;

    next = UINT_MAX;
    for (size_t t=0; t<no_tracks; ++t)
    {
        wait_parts = next;
        rv |= track[t]->process(time_parts, elapsed_parts, wait_parts);
        if (next > wait_parts) {
            next = wait_parts;
        }
    }

    if (next == UINT_MAX) {
        next = 100;
    }

    if (midi.get_verbose() && !midi.get_lyrics())
    {
        float hour, minutes, seconds;

        pos_sec += elapsed_parts*midi.get_uspp()*1e-6f;

        seconds = pos_sec;
        hour = floorf(seconds/(60.0f*60.0f));
        seconds -= hour*60.0f*60.0f;
        minutes = floorf(seconds/60.0f);
        seconds -= minutes*60.0f;
        if (hour) {
            MESSAGE("pos: %02.0f:%02.0f:%02.0f hours\r", hour, minutes, seconds);
        } else {
            MESSAGE("pos: %02.0f:%02.0f minutes\r", minutes, seconds);
        }
        if (!rv) MESSAGE("\n\n");
        fflush(stdout);
    }

    return rv;
}