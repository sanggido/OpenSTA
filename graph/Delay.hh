// OpenSTA, Static Timing Analyzer
// Copyright (c) 2018, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Define DELAY_FLOAT to use the float definitions.
#define DELAY_FLOAT

// Define DELAY_FLOAT_CLASS to use the Delay class definitions.
//#define DELAY_FLOAT_CLASS

#ifdef DELAY_FLOAT
 #include "DelayFloat.hh"
#endif

#ifdef DELAY_FLOAT_CLASS
 #include "DelayFloatClass.hh"
#endif
