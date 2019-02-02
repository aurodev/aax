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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <time.h>
#include <math.h>
#include <strings.h>

#include "driver.h"

static float _db2lin(float v) { return _MINMAX(powf(10.0f,v/20.0f),0.0f,10.0f); }

static const char* format_float6(float f)
{
    static char buf[32];

    if (f >= 100.0f) {
        snprintf(buf, 20, "%.1f", f);
    }
    else
    {
        snprintf(buf, 20, "%.6g", f);
        if (!strchr(buf, '.')) {
            strcat(buf, ".0");
        }
    }
    return buf;
}

void print_aaxs(const char* outfile, float db[9], char commons, char percussion, char overdrive, char leslie, char chorus, char reverb)
{
    float pitch[9] = { 0.5f, 0.75f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
    FILE *output;
    struct tm* tm_info;
    time_t timer;
    char year[5];
    float total;
    int i, num;

    time(&timer);
    tm_info = localtime(&timer);
    strftime(year, 5, "%Y", tm_info);

    if (outfile)
    {
        output = fopen(outfile, "w");
        testForError(output, "Output file could not be created.");
    }
    else {
        output = stdout;
    }

    fprintf(output, "<?xml version=\"1.0\"?>\n\n");

    fprintf(output, "<!--\n");
    fprintf(output, " * Copyright (C) 2017-%s by Erik Hofman.\n", year);
    fprintf(output, " * Copyright (C) 2017-%s by Adalin B.V.\n", year);
    fprintf(output, " * All rights reserved.\n");
    if (commons)
    {
        fprintf(output, " *\n");
        fprintf(output, " * This file is part of AeonWave and covered by the\n");
        fprintf(output, " * Creative Commons Attribution-ShareAlike 4.0 International Public License\n");
        fprintf(output, " * https://creativecommons.org/licenses/by-sa/4.0/legalcode\n");
    }
    fprintf(output, "-->\n\n");

    fprintf(output, "<aeonwave>\n\n");

    fprintf(output, " <info name=\"Drawbar\" bank=\"0\" program=\"0\">\n");
    fprintf(output, "  <note polyphony=\"10\" min=\"36\" max=\"96\" step=\"12\"/>\n");
    fprintf(output, " </info>\n\n");

    fprintf(output, " <sound gain=\"1.0\" frequency=\"220\" duration=\"0.1\">\n");

    num = 0;
    total = 0.0f;
    for (i=0; i<9; ++i) {
        if (db[i] > 0.f) ++num;
        total += _db2lin(-3.0f*(8.0f-db[i]));
    }
    total *= 0.5f;

    if (num)
    {
        for (i=0; i<9; ++i)
        {
            float v = _db2lin(-3.0f*(8.0f-db[i]))/total;
            if (db[i] > 0.f)
            {
                if (!i) {
                    fprintf(output, "  <waveform src=\"sine\" ratio=\"%s\"", format_float6(v));
                } else {
                    fprintf(output, "  <waveform src=\"sine\" processing=\"add\" ratio=\"%s\"", format_float6(v));
                }
                if (pitch[i] != 1.0f) {
                    fprintf(output, " pitch=\"%s\"", format_float6(pitch[i]));
                }
                else if (reverb) {
                    fprintf(output, " voices=\"3\"");
                }
                fprintf(output, "/>\n");
            }
        }
    }
    fprintf(output, " </sound>\n\n");

    fprintf(output, " <emitter looping=\"true\">\n");
    fprintf(output, "  <filter type=\"timed-gain\"");
    if (reverb) fprintf(output, " release-factor=\"7.0\"");
    fprintf(output, ">\n");
    fprintf(output, "   <slot n=\"0\">\n");
    fprintf(output, "    <param n=\"0\">%g</param>\n", percussion ? 1.5f : 0.25f);
    fprintf(output, "    <param n=\"1\">0.08</param>\n");
    fprintf(output, "    <param n=\"2\">1.2</param>\n");
    fprintf(output, "    <param n=\"3\">inf</param>\n");
    fprintf(output, "   </slot>\n");
    fprintf(output, "   <slot n=\"1\">\n");
    fprintf(output, "    <param n=\"0\">1.2</param>\n");
    fprintf(output, "    <param n=\"1\">%g</param>\n", reverb ? 0.7 : 0.2);
    fprintf(output, "    <param n=\"2\">0.0</param>\n");
    fprintf(output, "    <param n=\"3\">0.0</param>\n");
    fprintf(output, "   </slot>\n");
    fprintf(output, "  </filter>\n");
    fprintf(output, " </emitter>\n\n");

    fprintf(output, " <audioframe>\n");
    fprintf(output, "  <filter type=\"equalizer\" optional=\"true\">\n");
    fprintf(output, "   <slot n=\"0\">\n");
    fprintf(output, "    <param n=\"0\">100.0</param>\n");
    fprintf(output, "    <param n=\"1\">0.1</param>\n");
    fprintf(output, "    <param n=\"2\">1.0</param>\n");
    fprintf(output, "    <param n=\"3\">1.0</param>\n");
    fprintf(output, "   </slot>\n");
    fprintf(output, "   <slot n=\"1\">\n");
    fprintf(output, "    <param n=\"0\">3700.0</param>\n");
    fprintf(output, "    <param n=\"1\">1.0</param>\n");
    fprintf(output, "    <param n=\"2\">0.0</param>\n");
    fprintf(output, "    <param n=\"3\">1.0</param>\n");
    fprintf(output, "   </slot>\n");
    fprintf(output, "  </filter>\n");
    if (overdrive)
    {
        fprintf(output, "  <effect type=\"distortion\" optional=\"true\">\n");
        fprintf(output, "   <slot n=\"0\">\n");
        fprintf(output, "    <param n=\"0\">%g</param>\n", 0.1f+0.1f*overdrive);
        fprintf(output, "    <param n=\"1\">0.0</param>\n");
        fprintf(output, "    <param n=\"2\">0.15</param>\n");
        fprintf(output, "    <param n=\"3\">1.0</param>\n");
        fprintf(output, "   </slot>\n");
        fprintf(output, "  </effect>\n");
    }
    if (chorus)
    {
        fprintf(output, "  <effect type=\"chorus\" optional=\"true\">\n");
        fprintf(output, "   <slot n=\"0\">\n");
        fprintf(output, "    <param n=\"0\">0.6</param>\n");
        fprintf(output, "    <param n=\"1\">0.0</param>\n");
        fprintf(output, "    <param n=\"2\">0.0</param>\n");
        fprintf(output, "    <param n=\"3\">0.9</param>\n");
        fprintf(output, "   </slot>\n");
        fprintf(output, "  </effect>\n");
    }
    else if (leslie)
    {
        fprintf(output, "  <effect type=\"phasing\" src=\"sine\" optional=\"true\">\n");
        fprintf(output, "   <slot n=\"0\">\n");
        fprintf(output, "    <param n=\"0\">0.383</param>\n");
        fprintf(output, "    <param n=\"1\">%g</param>\n", (leslie == 1) ? 1.54f : 5.54f);
        fprintf(output, "    <param n=\"2\">0.15</param>\n");
        fprintf(output, "    <param n=\"3\">0.5</param>\n");
        fprintf(output, "   </slot>\n");
        fprintf(output, "  </effect>\n");
    }
    fprintf(output, " </audioframe>\n\n");

    fprintf(output, "</aeonwave>\n");

    if (outfile) {
        fclose(output);
    }
}

void help()
{
    printf("drawbar2aaxs version %i.%i.%i\n\n", AAX_UTILS_MAJOR_VERSION,
                                                    AAX_UTILS_MINOR_VERSION,
                                                    AAX_UTILS_MICRO_VERSION);
    printf("Usage: drawbar2aaxs [options]\n");
    printf("Creates an AAXS configuration file based on the drawbar organ\n");
    printf("drawbar settings.\n");

    printf("\nOptions:\n");
    printf(" -o, --output <file>\t\twrite the new .aaxs configuration to this file.\n");
    printf(" -d, --drawbar <XXXXXXXXX>\tUse these drawbar settings.\n");
    printf("     --omit-cc-by\t\tDo not add the CC-BY license reference.\n");
//  printf("     --chorus\t\t\tAdd the chorus effect.\n");
    printf("     --leslie <slow|fast>\tAdd the Leslie speaker in slow or fast mode.\n");
    printf("     --overdrive <mild|strong>\tAdd a mild or strong tube overdrive effect.\n");
    printf("     --percussion\t\tAdd the percussion effect.\n");
    printf("     --reverb\t\t\tAdd the reverb effect.\n");
    printf(" -h, --help\t\t\tprint this message and exit\n");

    printf("\nWhen no output file is specified then stdout will be used.\n");
    printf("Note: Either Leslie speaker or reverb can be used but not both.\n");
//  printf("      Reverb also turns on chorus automatically.\n");

    printf("\n");
    exit(-1);
}

int main(int argc, char **argv)
{
    char *s, *outfile, *drawbar;
    char percussion = 0;
    char overdrive = 0;
    char commons = 1;
    char leslie = 0;
    char chorus = 0;
    char reverb = 0;

    if (argc == 1 || getCommandLineOption(argc, argv, "-h") ||
                    getCommandLineOption(argc, argv, "--help"))
    {
        help();
    }

    if (getCommandLineOption(argc, argv, "--omit-cc-by")) {
        commons = 0;
    }

    if (getCommandLineOption(argc, argv, "--percussion")) {
        percussion = 1;
    }

    if (getCommandLineOption(argc, argv, "--chorus")) {
        chorus = 1;
    }

    if (getCommandLineOption(argc, argv, "--reverb")) {
        reverb = 1; chorus = 1;
    }

    s = getCommandLineOption(argc, argv, "--leslie");
    if (s)
    {
        if (!strcasecmp(s, "fast")) leslie = 2;
        else leslie = 1;
    }

    s = getCommandLineOption(argc, argv, "--overdrive");
    if (s)
    {
        if (!strcasecmp(s, "strong")) overdrive = 3;
        else if (!strcasecmp(s, "medium")) overdrive = 2;
        else overdrive = 1;
    }

    outfile = getOutputFile(argc, argv, NULL);
    drawbar = getCommandLineOption(argc, argv, "-d");
    if (!drawbar) {
        drawbar = getCommandLineOption(argc, argv, "--drawbar");
    }
        
    if (drawbar)
    {
        float db[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        int max, i;

        max = strlen(drawbar);
        if (max > 9) i = 9;
        for(i=0; i<max; ++i) {
            db[i] = (float)(drawbar[i] - '0');
        }

        print_aaxs(outfile, db, commons, percussion, overdrive, leslie, chorus, reverb);
    }
    else {
        help();
    }

    return 0;
}
