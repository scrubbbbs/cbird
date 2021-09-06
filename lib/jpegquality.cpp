/*************************************************************
 Copyright 2005-2007 Hacker Factor, All Rights Reserved.
 By Neal Krawetz, www.hackerfactor.com
 jpegquality:
 Estimate the quality of the JPEG based on the quantization tables.

 Neal's personal comment...
 "Jpeg, and Exif in particular, is one of the most screwed up formats
 that I have ever seen."

 Licensing and distribution:
 This code is provided as open source but is NOT licensed under the GPL
 or other common open source licenses.  This code may not be re-licensed
 without the explicit, written permission of Neal Krawetz.

 This software is provided AS-IS with no warranty expressed or implied.
 It may not be accurate and many not be suitable for any specific need.
 In locations where a warranty is required, this code may not be used.
 No known patent issues exist, but if some are discovered then this
 software may not be used.  The copyright holder is not liable for any
 costs associated with using this software.

 This code, or portions of it, may be incorporated into other projects as
 long as the code is not re-licensed and the following acknowledgement is
 included along with any licensing files, copyright statements, and
 source code:
    This software includes code from jpegquality by Neal Krawetz,
    Hacker Factor Solutions, Copyright 2005-2007.

 Compiling instructions:
    gcc jpegquality.c -o jpegquality

 Usage:
    jpegquality file.jpg
    You may list one or more jpeg files for analysis.

 Revision history:
   This code has been stripped out of imgana by Hacker Factor Solutions.
   (Imgana does much more than quality analysis, but that's all that is
   being released right now.)
 *************************************************************/
#include "jpegquality.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

//static int Debug=0;

#define	Abs(x)	((x) < 0 ? -(x) : (x))

#if JPEGQUALITY_PROG
/***************************************************
 Usage(): Display program usage.
 ***************************************************/
void	Usage	(char *Name)
{
    fprintf(stderr,"Usage: %s file.jpg [file.jpg file.jpg...]\n",Name);
} /* Usage() */
#endif

/***************************************************
 ReadJpegMarker(): Jpeg is a weird protocol.
 There are markers in the stream.
 Markers (1) begin with 0xff and (2) are not followed
 by 0x00 or 0xff.
 This function reads the next marker.
 ***************************************************/
static int	ReadJpegMarker	(FILE *Fin)
{
    int B1,B2;
    long Pos;
    long Len;

    Pos = ftell(Fin); Len=0;
    (void)Pos;
ReadAgainB1:
    B1 = fgetc(Fin); Len++;
    while(!feof(Fin) && (B1 != 0xff))
    {
        B1 = fgetc(Fin); Len++;
    }
    if (feof(Fin)) return(0);
ReadAgainB2:
    B2 = fgetc(Fin); Len++;
    if (B2 == 0xff)  goto ReadAgainB2;
    if (B2 == 0x00)  goto ReadAgainB1;
    return(B1*256+B2);
} /* ReadJpegMarker() */

/***************************************************
 ProcessJPEG(): Process a JPEG for comments
 Returns:  0=processed JPEG    1=not a JPEG!
 ***************************************************/
static void ProcessJPEG	(FILE *Fin, JpegQuality& result)
{
    int Header[15];
    int i;
    int Type;
    int Length;
    float Diff=0;  /* difference between quantization tables */
    float QualityAvg[3] = {0,0,0}; /* allow up to 3 quantization tables */
    float QualityF; /* quality as a float */
    int QualityI; /* quality as an integer */
    float Total;
    float TotalNum;
    int Precision,Index;

    /***
    JPEGs are in a fixed file format:
      6 bytes = header
      Optional global
      Set of blocks.
         Each block has a type, size, and data.

    The quantization tables are type 0xffdb.
    Each type 0xffdb may contain 1 or more quantization tables.
    The tables define the compression based in luminance (Y),
    chrominance-red (Cr) and chrominance-blue (Cb).
      - Most programs use two tables: one for Y and one for both Cr and Cb.
        Some programs use one 0xffdb with both tables in it, while other
        programs use one 0xffdb for each quantization table.
      - Some programs (mostly digital cameras) use three tables: Y, Cr, Cb.
        I have never seen a case where Cr != Cb, but it is possible.
        I usually see one 0xffdb with all 3 tables in it.
        However, it is possible to have two or three 0xffdb tags.
    This code does not check if there are 2 or 3 tables.  It blindly
    prints out the average after each chrominance table.
    If there are 3 tables, then you should look at the last average
    quality since it is computed correctly.

    In addition to multiple quantization tables per image, a JPEG may
    contain multiple images at different resolutions.  This program will
    pull out the quantization tables found in each image.
   ***/

    /* read the initial header */
    for(i=0; i<2; i++)
    {
        Header[i] = fgetc(Fin);
    }

    /* Check the header */
    /** JPEG header begins with "ffd8" **/
    if ((Header[0] != 0xFF) || (Header[1] != 0xD8))
    {
        qWarning("EstimateJpegQuality: Not a supported JPEG format");
        return;
    }

    // "reliable" unless there is a problem
    result.isReliable = true;

    /* Now, search for the quantization tables. */
    while(!feof(Fin))
    {
        /* All "Type" markers begin with "FF" and are followed by anything
       except 0x00 or 0xFF.  (Very weird standard.) */
        Type = ReadJpegMarker(Fin);
        if (Type==0)
        {
            qWarning("EstimateJpegQuality: Invalid type marker");
            return;
        }

        /* If it got here, then it is a quantization table, but validate the
       length just to be sure. */
        Length = fgetc(Fin) * 256 + fgetc(Fin); /* 2 bytes */
        /** The length is always too long by 2 bytes.  Weird standard. **/
        Length = Length - 2;
        if (Length < 0) Length=0;

        if (Type != 0xffdb)
        {
            /* not a quantization table */
            for(i=0; i<Length; i++) fgetc(Fin);
                continue;
        }

        if (Length%65 != 0)
        {
            qWarning("EstimateJpegQuality: Wrong size for quantization table --\n"
                     "this contains %d bytes (%d bytes short or %d bytes long)\n",
                     Length,65-Length%65,Length%65);
            result.isReliable = false;
        }

        /* Process quantization tables */
        //printf("\nQuantization table\n");

        /** precision is specified by the higher four bits and index is
        specified by the lower four bits **/
        while(Length > 0)
        {
            Precision = fgetc(Fin);
            Length--;
            Index = Precision & 0x0f;
            //Precision = (Precision & 0xf0) / 16;
            //printf("  Precision=%d; Table index=%d (%s)\n",Precision,Index,Index ? "chrominance":"luminance");

            /* Quantization tables have 1 DC value and 63 AC values */
            /** Average AC table values to estimate compression level **/
            Total=0;
            TotalNum=0;
            while((Length > 0) && (TotalNum<64))
            {
                i = fgetc(Fin);
                if (TotalNum!=0) Total += i; /* ignore first value */
                Length--;
                /* Show quantization table */
                //if (((int)TotalNum%8) == 0) printf("    ");
                //printf("%4d",i);
                result.table[Index].append(i);

                //if (((int)TotalNum%8) == 7) printf("\n");
                TotalNum++;
            }
            TotalNum--; /* we read 64 bytes, but only care about 63 values */
            if (Index < 3) /* Only track the first 3 quantization tables */
            {
                QualityAvg[Index] = 100.0-Total/TotalNum;
                //printf("  Estimated quality level = %5.2f%%\n",QualityAvg[Index]);
                if (QualityAvg[Index] <= 0)
                {
                    result.isReliable = false;
                    qWarning("EstimateJpegQuality: Quality too low; estimate may be incorrect.");

                }
                /* copy over the Q tables for initialization (in case Cr==Cb) */
                for(i=Index+1; i<3; i++) QualityAvg[i]=QualityAvg[Index];
            }

            /*****
       Technically, JPEG uses YCrCb.
         R = Y + (R-Y) = Y + Cr
         G = Y - 0.51(R-Y) - 0.186(B-Y) = Y - 0.51Cr -0.186Cb
         B = Y + (B-Y) = Y + Cb
       and in reverse (YCrCb):
         Luminance = Y = 0.299R + 0.587G + 0.114B
         Cr = R-Y = R - (0.299R + 0.587G + 0.114B)
         Cb = B-Y = B - (0.299R + 0.587G + 0.114B)
       The quantization tables are based on Y and CrCb and
       they mainly differ by a factor of 0.51 (from determining G).
       Thus, we compute the difference using 1 - 0.51 = 0.49.
       The results will be off by a fraction, but that's noise
       considering all of the integer rounding.
    *****/
            if (Index > 0)
            {
                /* Diff is a really rough estimate for converting YCrCb to RGB */
                Diff  = Abs(QualityAvg[0]-QualityAvg[1]) * 0.49;
                Diff += Abs(QualityAvg[0]-QualityAvg[2]) * 0.49;
                /* If you know that Cr==Cb and don't mind a little more error,
           then you can take a short-cut and simply say
           Diff = Abs(QualityAvg[0]-QualityAvg[1]); */
                QualityF = (QualityAvg[0]+QualityAvg[1]+QualityAvg[2])/3.0 + Diff;
                QualityI = (QualityF+0.5); /* round quality to int */
                //printf("Average quality: %5.2f%% (%d%%)\n",QualityF,QualityI);
                result.quality = QualityI;
                result.ok = true;

            } /* if all tables loaded */
        } /* for each set of 65 bytes */
    } /* while read file */

} /* ProcessJPEG() */

#if JPEGQUALITY_PROG
/**************************************************************/
int	main	(int argc, char *argv[])
{
    int c;
    FILE *Fin;
    int rc;

    /* process command lines */
    /** Uh, imgana has command line options, but jpegquality does not **/
    opterr=0;
    while ((c=getopt(argc,argv,"")) != -1)
    {
        switch (c)
        {
        default:
            Usage(argv[0]);
            exit(-1);
        }
    }

    /* process each file */
    if (argc - optind < 1)
    {
        Usage(argv[0]);
        exit(-1);
    }

    for( ; optind < argc; optind++)
    {
        Fin = fopen(argv[optind],"rb");
        if (!Fin)
        {
            fprintf(stderr,"ERROR: failed to open %s\n",argv[optind]);
            continue;
        }
        printf("#File: %s\n",argv[optind]);
        rc=ProcessJPEG(Fin);
        (void)rc;
        fclose(Fin);
        if (optind+1 < argc) printf("\n");
    }
    return(0);
} /* main() */
#else

JpegQuality EstimateJpegQuality(const QString& filePath)
{
    JpegQuality result;

    QFile qFile(filePath);
    if (qFile.open(QFile::ReadOnly))
    {
        FILE* fp = fdopen(qFile.handle(), "rb");
        if (fp)
        {
            ProcessJPEG(fp, result);
            fclose(fp);
        }
    }

    return result;
}

#endif
