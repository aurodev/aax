/*
 * Copyright (C) 2018 by Erik Hofman.
 * Copyright (C) 2018 by Adalin B.V.
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

#include <xml.h>

#include <base/timer.h>
#include "midi.hpp"


MIDI::MIDI(const char* n) : aax::AeonWave(n)
{
    channels.resize(16);
    channels.at(0x9) = new MIDIChannel(*this, 0x9, 0, 0);
    aax::AeonWave::add(channel(0x9));
}

MIDIChannel&
MIDI::new_channel(uint8_t channel_no, uint8_t bank_no, uint8_t program_no)
{
    if (channel_no >= channels.size()) {
        throw(std::out_of_range("index beyond buffer length"));
        channels.resize(channel_no+1);
    }

    try {
        channels.at(channel_no) = new MIDIChannel(*this, channel_no, bank_no, program_no);
        aax::AeonWave::add(channel(channel_no));
    } catch(const std::invalid_argument& e) {
        throw;
    }

    return *channels.at(channel_no);
}

MIDIChannel&
MIDI::channel(uint8_t channel_no)
{
    if (channel_no >= channels.size()) {
        throw(std::out_of_range("index beyond buffer length"));
        channels.resize(channel_no+1);
    }
    return *channels.at(channel_no);
}

bool
MIDI::process(uint8_t channel_no, uint8_t message, uint8_t key, uint8_t velocity, bool omni)
{
    switch(message)
    {
    case MIDI_NOTE_ON:
        if (omni) {
            for (auto& it : channels) {
                it->play(key, velocity);
            }
        } else {
            channel(channel_no).play(key, velocity);
        }
        break;
    case MIDI_NOTE_OFF:
        if (omni) {
            for (auto& it : channels) {
                it->stop(key);
            }
        } else {
            channel(channel_no).stop(key);
        }
        break;
    default:
        break;
    }
    return true;
}


std::string
MIDIChannel::get_name_from_xml(std::string& path, const char* type, uint8_t bank_no, uint8_t program_no)
{
    void *xid = xmlOpen(path.c_str());
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
                    long int n = xmlAttributeGetInt(xbid, "n");
                    if (n == bank_no)
                    {
                        unsigned int inum=xmlNodeGetNum(xbid, type);
                        void *xiid = xmlMarkId(xbid);
                        for (unsigned int i=0; i<inum; i++)
                        {
                            if (xmlNodeGetPos(xbid, xiid, type, i) != 0)
                            {
                                long int n = xmlAttributeGetInt(xiid, "n");
                                if (n == program_no)
                                {
                                    unsigned int slen;

                                    slen = xmlAttributeCopyString(xiid, "file", file, 64);
                                    if (slen) {
                                        file[slen] = 0;
                                    }
                                    break;
                                }
                            }
                        }
                        xmlFree(xiid);
                    }
                    break;
                }
            }
            xmlFree(xbid);
            xmlFree(xaid);
        }
        else {
            std::cerr << "aeonwave/midi not found in: " << path << std::endl;
        }
        xmlClose(xid);
        if (file[0] != 0) {
            return file;
        }
    }
    else {
        std::cerr << "Unable to open: " << path << std::endl;
    }
    return ""; // "instruments/piano-acoustic"
}

std::string
MIDIChannel::get_name(uint8_t channel, uint8_t bank_no, uint8_t program_no)
{
//      std::string path(midi.info(AAX_SHARED_DATA_DIR));
    std::string path("/usr/share/aax");
    path.append("/");
    if (channel == 0x9) // drums
    {
        path.append("gmdrums.xml");
        return get_name_from_xml(path, "drum", bank_no, program_no);
    }
    else                // instruments
    {
        path.append("gmmidi.xml");
        return get_name_from_xml(path, "instrument", bank_no, program_no);
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

bool
MIDITrack::process(uint32_t time_pos)
{
    bool rv = !eof();

    while (!eof() && (timestamp <= time_pos))
    {
        uint8_t data, message = pull_byte();

        if ((message & 0x80) == 0) {
           push_byte();
           message = previous;
        } else {
           previous = message;
        }

        rv = true;

        switch(message)
        {
        case MIDI_EXCLUSIVE_MESSAGE:
        case MIDI_EXCLUSIVE_MESSAGE_END:
        {
            uint8_t size = pull_byte();
            forward(size);
            break;
        }
        case MIDI_SYSTEM_MESSAGE:
        {
            uint8_t meta = pull_byte();
            uint8_t size = pull_byte();
            switch(meta)
            {
            case MIDI_TEXT:
            case MIDI_COPYRIGHT:
            case MIDI_TRACK_NAME:
            case MIDI_INSTRUMENT_NAME:
            case MIDI_LYRICS:
            case MIDI_MARKER:
            case MIDI_CUE_POINT:
            case MIDI_DEVICE_NAME:
                forward(size);			// not implemented yet
                break;
            case MIDI_CHANNEL_PREFIX:
                channel_no = (channel_no & 0xF0) | pull_byte();
                break;
            case MIDI_PORT_PREFERENCE:
                channel_no = (channel_no & 0xF) | pull_byte() << 8;
                break;
            case MIDI_END_OF_TRACK:
                forward();
                break;
            case MIDI_SET_TEMPO:
            {
                uint32_t tempo;
                tempo = (pull_byte() << 16) | (pull_byte() << 8) | pull_byte();
                bpm = tempo2bpm(tempo);
                break;
            }
            case MIDI_SEQUENCE_NUMBER:
            case MIDI_TIME_SIGNATURE: 
            case MIDI_SMPTE_OFFSET:
            case MIDI_KEY_SIGNATURE:
            default:	// unsupported
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
            case MIDI_NOTE_ON:
            {
                uint8_t key = pull_byte();
                uint8_t velocity = pull_byte();
                 midi.process(channel, message & 0xf0, key, velocity, omni);
                break;
            }
            case MIDI_POLYPHONIC_PRESSURE:
            {
                uint8_t key = pull_byte();
                uint8_t pressure = pull_byte();
                midi.channel(channel).set_pressure(key, pressure);
                break;
            }
            case MIDI_CONTROL_CHANGE:
            {
                uint8_t controller = pull_byte();
                uint8_t value = pull_byte();
                switch(controller)
                {
                case MIDI_ALL_SOUND_OFF:
                case MIDI_OMNI_OFF:
                    omni = false;
                    break;
                case MIDI_OMNI_ON:
                    omni = true;
                    break;
                default:
                    break;
                }
                break;
            }
            case MIDI_PROGRAM_CHANGE:
            {
                uint8_t program_no = pull_byte();
                try {
                    midi.new_channel(channel, bank_no, program_no);
                } catch(const std::invalid_argument& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                }
                break;
            }
            case MIDI_CHANNEL_PRESSURE:
            {
                uint8_t pressure = pull_byte();
                midi.channel(channel).set_pressure(pressure);
                break;
            }
            case MIDI_PITCH_BEND:
            {
                uint16_t pitch = pull_byte() << 7 | pull_byte();
                float p = semi_tones*((float)pitch-8192);
                if (p < 0) p /= 8192.0f;
                else p /= 8191.0f;
                midi.channel(channel).set_pitch(powf(2.0f, p/12.0f));
                break;
            }
            case MIDI_SYSTEM:
                switch(channel)
                {
                case MIDI_TIMING_CLOCK:
                case MIDI_START:
                case MIDI_CONTINUE:
                case MIDI_STOP:
                case MIDI_ACTIVE_SENSE:
                case MIDI_SYSTEM_RESET:
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
            break;
        } // switch
        } // default

        if (!eof())
        {
            uint32_t parts = pull_message();
            timestamp += parts*(60000.0f/(bpm*PPQN));
        }
    }

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
                    uint32_t header = stream.pull_long();
                    uint16_t track_no = 0;
                    uint16_t PPQN = 24;

                    if (header == 0x4d546864) // "MThd"
                    {
                        stream.forward(4); // skip the size;

                        format = stream.pull_word();
                        no_tracks = stream.pull_word();
                        PPQN = stream.pull_word();
                    }
                
                    while (!stream.eof())
                    {
                        header = stream.pull_long();
                        if (header == 0x4d54726b) // "MTrk"
                        {
                            uint32_t length = stream.pull_long();
                            track.push_back(new MIDITrack(*this, stream, length, track_no++, PPQN));
                            stream.forward(length);
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

bool
MIDIFile::process(uint32_t time)
{
    bool rv = false;
    for (size_t t=0; t<no_tracks; ++t) {
        rv |= track[t]->process(time);
    }
    return rv;
}

