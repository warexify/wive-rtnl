/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
/* Macros for the 'type' part of an fopen, freopen or fdopen. 

	<Read|Write>[Update]<Binary file|text file>

   This version is for VMS systems, where text and binary files are
   different.
   This file is designed for inclusion by host-dependent .h files.  No
   user application should include it directly, since that would make
   the application unable to be configured for both "same" and "binary"
   variant systems.  */

#define FOPEN_RB	"rb","rfm=var"
#define FOPEN_WB 	"wb","rfm=var"
#define FOPEN_AB 	"ab","rfm=var"
#define FOPEN_RUB 	"r+b","rfm=var"
#define FOPEN_WUB 	"w+b","rfm=var"
#define FOPEN_AUB 	"a+b","rfm=var"

#define FOPEN_RT	"r"
#define FOPEN_WT 	"w"
#define FOPEN_AT 	"a"
#define FOPEN_RUT 	"r+"
#define FOPEN_WUT 	"w+"
#define FOPEN_AUT 	"a+"