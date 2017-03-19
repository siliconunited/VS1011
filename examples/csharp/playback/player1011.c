/*

  VLSI Solution generic microcontroller example player / recorder for
  VS1011.

  v1.10 2016-05-09 HH  Added chip type recognition, modified quick sanity check
  v1.00 2012-11-28 HH  First release

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "player.h"


#define FILE_BUFFER_SIZE 512
#define SDI_MAX_TRANSFER_SIZE 32
#define SDI_END_FILL_BYTES 512 /* Arbitrarily chosen value */


/* How many transferred bytes between collecting data.
   A value between 1-8 KiB is typically a good value.
   If REPORT_ON_SCREEN is defined, a report is given on screen each time
   data is collected. */
#define REPORT_INTERVAL 4096
#if 1
#define REPORT_ON_SCREEN
#endif

/* Define PLAYER_USER_INTERFACE if you want to have a user interface in your
   player. */
#if 1
#define PLAYER_USER_INTERFACE
#endif


#define min(a,b) (((a)<(b))?(a):(b))



enum AudioFormat {
  afUnknown,
  afRiff,
  afMp1,
  afMp2,
  afMp3,
} audioFormat = afUnknown;

const char *afName[] = {
  "unknown",
  "RIFF",
  "MP1",
  "MP2",
  "MP3",
};




enum PlayerStates {
  psPlayback = 0,
  psUserRequestedCancel,
  psCancelSentToVS10xx,
  psStopped
} playerState;





/*

  This function plays back an audio file.

  It also contains a simple user interface, which requires the following
  funtions that you must provide:
  void SaveUIState(void);
  - saves the user interface state and sets the system up
  - may in many cases be implemented as an empty function
  void RestoreUIState(void);
  - Restores user interface state before exit
  - may in many cases be implemented as an empty function
  int GetUICommand(void);
  - Returns -1 for no operation
  - Returns -2 for cancel playback command
  - Returns any other for user input. For supported commands, see code.

*/
void VS1011PlayFile(FILE *readFp) {
  static u_int8 playBuf[FILE_BUFFER_SIZE];
  u_int32 bytesInBuffer;        // How many bytes in buffer left
  u_int32 pos=0;                // File position
  long nextReportPos=0; // File pointer where to next collect/report
  int i;
#ifdef PLAYER_USER_INTERFACE
  int volLevel = ReadSci(SCI_VOL) & 0xFF; // Assume both channels at same level
  int c;
#endif /* PLAYER_USER_INTERFACE */

#ifdef PLAYER_USER_INTERFACE
  SaveUIState();
#endif /* PLAYER_USER_INTERFACE */

  playerState = psPlayback;             // Set state to normal playback

  WriteSci(SCI_DECODE_TIME, 0);         // Reset DECODE_TIME


  /* Main playback loop */

  while ((bytesInBuffer = fread(playBuf, 1, FILE_BUFFER_SIZE, readFp)) > 0 &&
         playerState != psStopped) {
    u_int8 *bufP = playBuf;

    while (bytesInBuffer && playerState != psStopped) {
      {
        int t = min(SDI_MAX_TRANSFER_SIZE, bytesInBuffer);

        // This is the heart of the algorithm: on the following line
        // actual audio data gets sent to VS10xx.
        WriteSdi(bufP, t);

        bufP += t;
        bytesInBuffer -= t;
        pos += t;
      }

      /* If the user has requested cancel, set VS10xx SM_OUTOFWAV bit */
      if (playerState == psUserRequestedCancel) {
        if (audioFormat == afMp3 || audioFormat == afUnknown) {
          playerState = psStopped;
        } else {
          unsigned short oldMode;
          playerState = psCancelSentToVS10xx;
          printf("\nSetting SM_OUTOFWAV at file offset %ld\n", pos);
          oldMode = ReadSci(SCI_MODE);
          WriteSci(SCI_MODE, oldMode | SM_OUTOFWAV);
        }
      }

      /* If VS10xx SM_OUTOFWAV bit has been set, see if it has gone
         through. If it is, it is time to stop playback. */
      if (playerState == psCancelSentToVS10xx) {
        unsigned short mode = ReadSci(SCI_MODE);
        if (!(mode & SM_OUTOFWAV)) {
          printf("SM_OUTOFWAV has cleared at file offset %ld\n", pos);
          playerState = psStopped;
        }
      }


      /* If playback is going on as normal, see if we need to collect and
         possibly report */
      if (playerState == psPlayback && pos >= nextReportPos) {
#ifdef REPORT_ON_SCREEN
        u_int16 sampleRate;
        u_int16 h1 = ReadSci(SCI_HDAT1);
#endif

        nextReportPos += REPORT_INTERVAL;

#ifdef REPORT_ON_SCREEN
        if (h1 == 0x7665) {
          audioFormat = afRiff;
        } else if ((h1 & 0xffe6) == 0xffe2) {
          audioFormat = afMp3;
        } else if ((h1 & 0xffe6) == 0xffe4) {
          audioFormat = afMp2;
        } else if ((h1 & 0xffe6) == 0xffe6) {
          audioFormat = afMp1;
        } else {
          audioFormat = afUnknown;
        }

        sampleRate = ReadSci(SCI_AUDATA);

        printf("\r%ldKiB "
               "%1ds %dHz %s %s"
               " %04x   ",
               pos/1024,
               ReadSci(SCI_DECODE_TIME),
               sampleRate & 0xFFFE, (sampleRate & 1) ? "stereo" : "mono",
               afName[audioFormat], h1
               );
          
        fflush(stdout);
#endif /* REPORT_ON_SCREEN */
      }
    } /* if (playerState == psPlayback && pos >= nextReportPos) */
  


    /* User interface. This can of course be completely removed and
       basic playback would still work. */

#ifdef PLAYER_USER_INTERFACE
    /* GetUICommand should return -1 for no command and -2 for CTRL-C */
    c = GetUICommand();
    switch (c) {

      /* Volume adjustment */
    case '-':
      if (volLevel < 255) {
        volLevel++;
        WriteSci(SCI_VOL, volLevel*0x101);
      }
      break;
    case '+':
      if (volLevel) {
        volLevel--;
        WriteSci(SCI_VOL, volLevel*0x101);
      }
      break;

      /* Show some interesting registers */
    case '_':
      printf("\nvol %1.1fdB, MODE %04x, ST %04x, "
             "HDAT1 %04x HDAT0 %04x\n",
             -0.5*volLevel,
             ReadSci(SCI_MODE),
             ReadSci(SCI_STATUS),
             ReadSci(SCI_HDAT1),
             ReadSci(SCI_HDAT0));
      break;

      /* Ask player nicely to stop playing the song. */
    case 'q':
      if (playerState == psPlayback)
        playerState = psUserRequestedCancel;
      break;

      /* Forceful and ugly exit. For debug uses only. */
    case 'Q':
      RestoreUIState();
      printf("\n");
      exit(EXIT_SUCCESS);
      break;

      /* Toggle differential mode */
    case 'd':
      {
        u_int16 t = ReadSci(SCI_MODE) ^ SM_DIFF;
        printf("\nDifferential mode %s\n", (t & SM_DIFF) ? "on" : "off");
        WriteSci(SCI_MODE, t);
      }
      break;

      /* Show help */
    case '?':
      printf("\nInteractive VS1011 file player keys:\n"
             "- +\tVolume down / up\n"
             "_\tShow current settings\n"
             "q Q\tQuit current song / program\n"
             "d\tToggle Differential\n"
             );
      break;

      /* Unknown commands or no command at all */
    default:
      if (c < -1) {
        printf("Ctrl-C, aborting\n");
        fflush(stdout);
        RestoreUIState();
        exit(EXIT_FAILURE);
      }
      if (c >= 0) {
        printf("\nUnknown char '%c' (%d)\n", isprint(c) ? c : '.', c);
      }
      break;
    } /* switch (c) */
#endif /* PLAYER_USER_INTERFACE */
  } /* while ((bytesInBuffer = fread(...)) > 0 && playerState != psStopped) */


  
#ifdef PLAYER_USER_INTERFACE
  RestoreUIState();
#endif /* PLAYER_USER_INTERFACE */

  printf("\nSending %d footer %d's... ", SDI_END_FILL_BYTES, 0);
  fflush(stdout);

  /* Earlier we collected endFillByte. Now, just in case the file was
     broken, or if a cancel playback command has been given, write
     lots of endFillBytes. */
  memset(playBuf, 0, sizeof(playBuf));
  for (i=0; i<SDI_END_FILL_BYTES; i+=SDI_MAX_TRANSFER_SIZE) {
    WriteSdi(playBuf, SDI_MAX_TRANSFER_SIZE);
  }

  /* If SM_OUTOFWAV is on at this point, there is some weirdness going
     on. Reset the IC just in case. */
  if (ReadSci(SCI_MODE) & SM_OUTOFWAV) {
    VSTestInitSoftware();
  }

  /* That's it. Now we've played the file as we should, and left VS10xx
     in a stable state. It is now safe to call this function again for
     the next song, and again, and again... */
  printf("ok\n");
}






/*
  This function does very little with VS1011.
*/
void VS1011RecordFile(FILE *writeFp) {
  printf("VS1011 does not support recording.\n");
}





/*

  Hardware Initialization for VS1011.

  
*/
int VSTestInitHardware(void) {
  /* Write here your microcontroller code which puts VS10xx in hardware
     reset anc back (set xRESET to 0 for at least a few clock cycles,
     then to 1). */
  return 0;
}



/* Note: code SS_VER=2 is used for both VS1002 and VS1011e */
const u_int16 chipNumber[16] = {
  1001, 1011, 1011, 1003, 1053, 1033, 1063, 1103,
  0, 0, 0, 0, 0, 0, 0, 0
};

/*

  Software Initialization for VS1011.

  Note that you need to check whether SM_SDISHARE should be set in
  your application or not.
  
*/
int VSTestInitSoftware(void) {
  u_int16 ssVer;

  /* Start initialization with a dummy read, which makes sure our
     microcontoller chips selects and everything are where they
     are supposed to be and that VS10xx's SCI bus is in a known state. */
  ReadSci(SCI_MODE);

  /* First real operation is a software reset. After the software
     reset we know what the status of the IC is. You need, depending
     on your application, either set or not set SM_SDISHARE. See the
     Datasheet for details. */
  WriteSci(SCI_MODE, SM_SDINEW|SM_SDISHARE|SM_TESTS|SM_RESET);

  /* A quick sanity check: write to two registers, then test if we
     get the same results. Note that if you use a too high SPI
     speed, the MSB is the most likely to fail when read again. */
  WriteSci(SCI_AICTRL1, 0xABAD);
  WriteSci(SCI_AICTRL2, 0x7E57);
  if (ReadSci(SCI_AICTRL1) != 0xABAD || ReadSci(SCI_AICTRL2) != 0x7E57) {
    printf("There is something wrong with VS10xx SCI registers\n");
    return 1;
  }
  WriteSci(SCI_AICTRL1, 0);
  WriteSci(SCI_AICTRL2, 0);

  /* Check VS10xx type */
  ssVer = ((ReadSci(SCI_STATUS) >> 4) & 15);
  if (chipNumber[ssVer]) {
    printf("Chip is VS%d\n", chipNumber[ssVer]);
    if (chipNumber[ssVer] != 1011) {
      printf("Incorrect chip\n");
      return 1;
    }
  } else {
    printf("Unknown VS10xx SCI_MODE field SS_VER = %d\n", ssVer);
    return 1;
  }

  /* Set the clock. Until this point we need to run SPI slow so that
     we do not exceed the maximum speeds mentioned in
     Chapter SPI Timing Diagram in the Datasheet. */
  WriteSci(SCI_CLOCKF, HZ_TO_SCI_CLOCKF(12288000));

  /* Set volume level at -6 dB of maximum */
  WriteSci(SCI_VOL, 0x0c0c);

  /* We're ready to go. */
  return 0;
}





/*
  Main function that activates either playback or recording.
*/
int VSTestHandleFile(const char *fileName, int record) {
  if (!record) {
    FILE *fp = fopen(fileName, "rb");
    printf("Play file %s\n", fileName);
    if (fp) {
      VS1011PlayFile(fp);
    } else {
      printf("Failed opening %s for reading\n", fileName);
      return -1;
    }
  } else {
    FILE *fp = fopen(fileName, "wb");
    printf("Record file %s\n", fileName);
    if (fp) {
      VS1011RecordFile(fp);
    } else {
      printf("Failed opening %s for writing\n", fileName);
      return -1;
    }
  }
  return 0;
}
