/*
*  Copyright (C) 2000 Brian Harris
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "string.h"
#include "Utils.h"

void TranslateTabs(char *buffer){
  char *p;
  for (p=buffer; *p; p++){
    if (*p == '\\'){
      if (*(p+1) == 't'){
        *p = ' ';
        *(p+1) = '\t';
      }
    }
  }
}

void TrimWhiteSpace(char *string){
  char *p;
  for ( p = string + strlen(string) - 1; p >= string; p-- ){
    if (*p == ' ' || *p == '\t'){
      *p = 0;
    }else{
      break;
    }
  }
}

char *SkipWhiteSpace(char *string){
  char *p;
  for (p = string; *p; p++){
    if (*p != ' ' && *p != '\t'){
      return p;
    }
  }
  return string;
}


//


int CondenseString(char *buf, int size) {
	int firstlen, secondlen, len;
   char *read, *write;

   write=buf+1;
   for (read=buf+1; *read; read++) {    // condense tabs and spaces
      if ( (*read == ' ') || (*read == '\t') ) {
         if (*(write-1) != *read) {    // if we've not alreade added a space
            *write = *read;
            write++;
         }
      }
      else {
         *write = *read;
         *write++;
      }
   }
   *(write+1) = 0;

   len = strlen(buf);
   if ((size == 0) || (len < size))
      return len;


   if (size%2) { // if even {
      firstlen = ((size +1) - 3) / 2;
      secondlen= firstlen-1;
   }
   else {
      firstlen = (size - 3) / 2;
      secondlen= firstlen;
   }

   memcpy(buf+firstlen, "...", 3);
   strcpy(buf+firstlen+3, buf+len-secondlen-1);
   
   return strlen(buf);

}