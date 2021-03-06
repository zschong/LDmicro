//-----------------------------------------------------------------------------
// Copyright 2007 Jonathan Westhues
//
// This file is part of LDmicro.
//
// LDmicro is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// LDmicro is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with LDmicro.  If not, see <http://www.gnu.org/licenses/>.
//------
//
// Routines common to the code generators for all processor architectures.
// Jonathan Westhues, Nov 2004
//-----------------------------------------------------------------------------
#include "stdafx.h"

#include "ldmicro.h"
#include "intcode.h"
#include "compilercommon.hpp"
#include "compilerexceptions.hpp"


// Assignment of the internal relays to memory, efficient, one bit per
// relay.
static struct {
    char  name[MAX_NAME_LEN];
    DWORD addr;
    int   bit;
    bool  assignedTo;
} InternalRelays[MAX_IO];
static int InternalRelayCount;
/*
vvv VariablesList moved to ldmicro.h

// Assignment of the `variables,' used for timers, counters, arithmetic, and
// other more general things. Allocate 2 octets (16 bits) per.
static struct {
    char    name[MAX_NAME_LEN];
    DWORD   addrl;
    DWORD   addrh;
} Variables[MAX_IO];

^^^ VariablesList moved to ldmicro.h
*/
VariablesList Variables[MAX_IO];
static int    VariableCount = 0;

#define NO_MEMORY 0xffffffff
static DWORD NextBitwiseAllocAddr;
static int   NextBitwiseAllocBit;
static int   MemOffset;
DWORD        RamSection;

//-----------------------------------------------------------------------------
int McuPWM()
{
    if(!Prog.mcu)
        return 0;

    int n = 0;
    if(Prog.mcu->pwmCount) {
        int prevPin = -1;
        for(uint32_t i = 0; i < Prog.mcu->pwmCount; i++) {
            if(Prog.mcu->pwmInfo[i].pin)
                if(Prog.mcu->pwmInfo[i].pin != prevPin)
                    if((Prog.mcu->whichIsa == ISA_PIC16) || (Prog.mcu->pwmInfo[i].timer != Prog.cycleTimer))
                        n++;
            prevPin = Prog.mcu->pwmInfo[i].pin;
        }
    } else if(Prog.mcu->pwmNeedsPin) {
        n = 1;
    }
    return n;
}

int McuADC()
{
    if(!Prog.mcu)
        return 0;

    return Prog.mcu->adcCount;
}

int McuSPI()
{
    if(!Prog.mcu)
        return 0;

    return Prog.mcu->spiCount;
}

int McuUART()
{
    if(!Prog.mcu)
        return 0;

    int n = 0;
    if(Prog.mcu->uartNeeds.rxPin && Prog.mcu->uartNeeds.txPin) {
        n = 1;
    }
    return n;
}

int McuROM()
{
    return 1000000; // TODO

    if(!Prog.mcu)
        return 0;

    int   n = 0;
    DWORD i;
    for(i = 0; i < MAX_ROM_SECTIONS; i++) {
        n += Prog.mcu->rom[i].len;
    }
    return n;
}

int UsedROM()
{
    if(!Prog.mcu)
        return 0;

    int   n = 0;
    DWORD i;
    for(i = 0; i < RomSection; i++) {
        n += Prog.mcu->rom[i].len;
    }
    return n + EepromAddrFree;
}

int McuRAM()
{
    if(!Prog.mcu)
        return 0;

    int   n = 0;
    DWORD i;
    for(i = 0; i < MAX_RAM_SECTIONS; i++) {
        n += Prog.mcu->ram[i].len;
    }
    return n;
}

int UsedRAM()
{
    if(!Prog.mcu)
        return 0;

    int   n = 0;
    DWORD i;
    for(i = 0; i < RamSection; i++) {
        n += Prog.mcu->ram[i].len;
    }
    return n + MemOffset;
}

//-----------------------------------------------------------------------------
void PrintVariables(FILE *f)
{
    int i;
    fprintf(f, "\n");
    fprintf(f,
            ";|  # | Name                                                    | Size      | Address      | Bit # |\n");

    fprintf(f, ";|Variables: %d\n", VariableCount);
    for(i = 0; i < VariableCount; i++) {
        if(Variables[i].addrl)
            fprintf(f,
                    ";|%3d | %-50s\t| %3d byte  | 0x%04X       |\n",
                    i,
                    Variables[i].name,
                    Variables[i].SizeOfVar,
                    Variables[i].addrl);
        /*
        else {
            DWORD addr;
            int   bit;
            bool forRead;
            forRead = false;
            switch(Variables[i].name[0]) {
                case 'I':
                case 'X':
                    forRead = true;
                    break;
                default:
                    break;
            }
            MemForSingleBit(Variables[i].name, forRead, &addr, &bit);
            fprintf(f, ";|%3d %-50s\t| %3d bit   | 0x%04X = %3d | %d     |\n", i, Variables[i].name, 1, addr, addr, bit);
        }
        */
    }
    fprintf(f, "\n");

    fprintf(f, ";|Internal Relays: %d\n", InternalRelayCount);
    for(i = 0; i < InternalRelayCount; i++) {
        fprintf(f,
                ";|%3d | %-50s\t| %3d bit   | 0x%04X       | %d     |\n",
                i,
                InternalRelays[i].name,
                1,
                InternalRelays[i].addr,
                InternalRelays[i].bit);
    }
    fprintf(f, "\n");
}
//-----------------------------------------------------------------------------
static void ClrInternalData()
{
    MemOffset = 0;
    RamSection = 0;
    RomSection = 0;
    EepromAddrFree = 0;
    //  VariableCount = 0;
    int i;
    for(i = 0; i < VariableCount; i++) {
        Variables[i].Allocated = 0;
        Variables[i].addrl = 0;
    }
}
//-----------------------------------------------------------------------------
// Forget what memory has been allocated on the target, so we start from
// everything free.
//-----------------------------------------------------------------------------
void AllocStart()
{
    NextBitwiseAllocAddr = NO_MEMORY;
    InternalRelayCount = 0;
    ClrInternalData();
    ClrSimulationData();
}

//-----------------------------------------------------------------------------
// Return the address of a previously unused octet of RAM on the target, or
// signal an error if there is no more available.
//-----------------------------------------------------------------------------
DWORD AllocOctetRam(int bytes) // The desired number of bytes.
{
    if(!Prog.mcu)
        return 0;

    if(Prog.mcu->whichIsa > ISA_HARDWARE)
        return 0;

    if((MemOffset + bytes) >= Prog.mcu->ram[RamSection].len) {
        RamSection++;
        MemOffset = 0;
    }

    if((RamSection >= MAX_RAM_SECTIONS) || ((MemOffset + bytes) >= Prog.mcu->ram[RamSection].len)) {
        THROW_COMPILER_EXCEPTION_FMT("%s %s",
                                     _("RAM:"),
                                     _("Out of memory; simplify program or choose "
                                       "microcontroller with more memory."));
    }

    MemOffset += bytes;
    return Prog.mcu->ram[RamSection].start + MemOffset - bytes;
}

DWORD AllocOctetRam()
{
    return AllocOctetRam(1);
}

//-----------------------------------------------------------------------------
int InputRegIndex(DWORD addr)
{
    if((addr == -1) || (addr == 0))
        THROW_COMPILER_EXCEPTION("Internal error.");
    for(int i = 0; i < MAX_IO_PORTS; i++)
        if(Prog.mcu->inputRegs[i] == addr)
            return i;
    return -1;
}

//-----------------------------------------------------------------------------
int OutputRegIndex(DWORD addr)
{
    if((addr == -1) || (addr == 0))
        THROW_COMPILER_EXCEPTION("Internal error.");
    for(int i = 0; i < MAX_IO_PORTS; i++)
        if(Prog.mcu->outputRegs[i] == addr)
            return i;
    return -1;
}

//-----------------------------------------------------------------------------
// Return the address (octet address) and bit of a previously unused bit of
// RAM on the target.
//-----------------------------------------------------------------------------
void AllocBitRam(DWORD *addr, int *bit)
{
    if(NextBitwiseAllocAddr != NO_MEMORY) {
        *addr = NextBitwiseAllocAddr;
        *bit = NextBitwiseAllocBit;
        NextBitwiseAllocBit++;
        if(NextBitwiseAllocBit > 7) {
            NextBitwiseAllocAddr = NO_MEMORY;
        }
        return;
    }

    *addr = AllocOctetRam();
    *bit = 0;
    NextBitwiseAllocAddr = *addr;
    NextBitwiseAllocBit = 1;
}

//-----------------------------------------------------------------------------
// Return the address (octet) and bit of the bit in memory that represents the
// given input or output pin. Raises an internal error if the specified name
// is not present in the I/O list or a user error if a pin has not been
// assigned to that I/O name. Will allocate if it no memory allocated for it
// yet, else will return the previously allocated bit.
//-----------------------------------------------------------------------------
static void MemForPin(const char *name, DWORD *addr, int *bit, bool asInput)
{
    int i;
    for(i = 0; i < Prog.io.count; i++) {
        if(strcmp(Prog.io.assignment[i].name, name) == 0)
            break;
    }
    if(i >= Prog.io.count)
        THROW_COMPILER_EXCEPTION("Internal error");

    if(asInput && Prog.io.assignment[i].type == IO_TYPE_DIG_OUTPUT)
        THROW_COMPILER_EXCEPTION("Internal error");
    if(!asInput && Prog.io.assignment[i].type != IO_TYPE_DIG_OUTPUT && Prog.io.assignment[i].type != IO_TYPE_PWM_OUTPUT)
        THROW_COMPILER_EXCEPTION("Internal error");

    *addr = -1;
    *bit = -1;
    if(Prog.mcu) {
        McuIoPinInfo *iop = PinInfo(Prog.io.assignment[i].pin);
        if(iop) {
            if(Prog.mcu->core == PC_LPT_COM) {
                *addr = iop->addr;
                *bit = iop->bit;
            } else {
                if(asInput) {
                    *addr = Prog.mcu->inputRegs[iop->port - 'A'];
                } else {
                    *addr = Prog.mcu->outputRegs[iop->port - 'A'];
                }
                *bit = iop->bit;
            }
        } else {
            THROW_COMPILER_EXCEPTION_FMT(_("Must assign pins for all I/O.\r\n\r\n"
                    "'%s' is not assigned."),
                  name);
        }
    }
}

void AddrBitForPin(int pin, DWORD *addr, int *bit, bool asInput)
{
    *addr = -1;
    *bit = -1;
    if(Prog.mcu) {
        McuIoPinInfo *iop = PinInfo(pin);
        if(iop) {
            if(asInput) {
                *addr = Prog.mcu->inputRegs[iop->port - 'A'];
            } else {
                *addr = Prog.mcu->outputRegs[iop->port - 'A'];
            }
            *bit = iop->bit;
        } else {
            if(pin)
                THROW_COMPILER_EXCEPTION_FMT(_("Not described pin %d "), pin);
        }
    }
}

//-----------------------------------------------------------------------------
int SingleBitAssigned(const char *name)
{
    int pin = 0;
    int i;
    for(i = 0; i < Prog.io.count; i++) {
        if(strcmp(Prog.io.assignment[i].name, name) == 0)
            break;
    }
    if(i >= Prog.io.count)
        THROW_COMPILER_EXCEPTION("Internal error");

    if(Prog.mcu) {
        pin = Prog.io.assignment[i].pin;
        auto pp = std::find_if(Prog.mcu->pinInfo,
                               Prog.mcu->pinInfo + Prog.mcu->pinCount,
                               [pin](const McuIoPinInfo &info) { return pin == info.pin; });
        if(pp == (Prog.mcu->pinInfo + Prog.mcu->pinCount))
            pin = 0;
    }
    return pin;
}

//-----------------------------------------------------------------------------
int GetAssignedType(const char *name, const char *fullName)
{
    int type = NO_PIN_ASSIGNED;
    if(fullName && strlen(fullName))
        if(fullName[0] == 'I') {
            if(fullName[1] == 'b')
                return IO_TYPE_INTERNAL_RELAY;
            else if(fullName[1] == 'i')
                return IO_TYPE_GENERAL;
            else
                THROW_COMPILER_EXCEPTION("Internal error");
        }
    for(int i = 0; i < Prog.io.count; i++) {
        if(strcmp(Prog.io.assignment[i].name, name) == 0) {
            type = Prog.io.assignment[i].type;
            break;
        }
    }
    return type;
}

//-----------------------------------------------------------------------------
// Determine the mux register settings to read a particular ADC channel.
//-----------------------------------------------------------------------------
uint8_t MuxForAdcVariable(const char *name)
{
    int res = 0;
    int i;
    for(i = 0; i < Prog.io.count; i++) {
        if((strcmp(Prog.io.assignment[i].name, name) == 0) &&
            (Prog.io.assignment[i].type == IO_TYPE_READ_ADC))
            break;
    }
    if(i >= Prog.io.count)
        THROW_COMPILER_EXCEPTION("Internal error");

    if(Prog.mcu) {
        uint32_t j;
        for(j = 0; j < Prog.mcu->adcCount; j++) {
            if(Prog.mcu->adcInfo[j].pin == Prog.io.assignment[i].pin) {
                break;
            }
        }
        if(j == Prog.mcu->adcCount) {
            Error("i=%d pin=%d", i, Prog.io.assignment[i].pin);
            THROW_COMPILER_EXCEPTION_FMT(_("Must assign pins for all ADC inputs (name '%s')."), name);
        }
        res = Prog.mcu->adcInfo[j].muxRegValue;
    }

    return res;
}

uint8_t MuxForAdcVariable(const NameArray &name)
{
    return MuxForAdcVariable(name.c_str());
}

//-----------------------------------------------------------------------------
int byteNeeded(long long int i)
{
    if((-128 <= i) && (i <= 127))
        return 1;
    else if((-32768 <= i) && (i <= 32767))
        return 2;
    else if((-8388608 <= i) && (i <= 8388607))
        return 3;
    else if((-2147483648LL <= i) && (i <= 2147483647LL))
        return 4; // not FULLY implamanted for LDmicro
    THROW_COMPILER_EXCEPTION("Internal error");
    return 0;
}

//-----------------------------------------------------------------------------
int TestByteNeeded(int count, SDWORD *vals)
{
    int res = -1;
    int i, r;
    for(i = 0; i < count; i++) {
        r = byteNeeded(vals[i]);
        if(res < r)
            res = r;
    }
    return res;
}

//-----------------------------------------------------------------------------
// Allocate 1,2,3 or 4 byte for a variable, used for a variety of purposes.
//-----------------------------------------------------------------------------
int MemForVariable(const char *name, DWORD *addrl, int sizeOfVar)
{
    if(!name)
        THROW_COMPILER_EXCEPTION("Internal error");
    if(strlenalnum(name) == 0) {
        THROW_COMPILER_EXCEPTION_FMT(_("Empty variable name '%s'.\nrungNow=%d"), name, rungNow + 1);
    }

    int i;
    for(i = 0; i < VariableCount; i++) {
        if((Variables[i].type == IO_TYPE_TABLE_IN_FLASH) || (Variables[i].type == IO_TYPE_VAL_IN_FLASH))
            Variables[i].Allocated = Variables[i].SizeOfVar;
    }
    for(i = 0; i < VariableCount; i++) {
        if(strcmp(name, Variables[i].name) == 0)
            break;
    }
    if(i >= MAX_IO) {
        THROW_COMPILER_EXCEPTION(_("Internal limit exceeded (number of vars)"));
    }
    if(i == VariableCount) {
        VariableCount++;
        memset(&Variables[i], 0, sizeof(Variables[i]));
        strcpy(Variables[i].name, name);
        if(name[0] == '#') {
            Variables[i].SizeOfVar = 1;
        }
    }
    if(sizeOfVar < 0) { // get addr, get size
        if(addrl)
            *addrl = Variables[i].addrl;

    } else if(sizeOfVar > 0) { // set size, set addr
        if(Variables[i].SizeOfVar == sizeOfVar) {
            // not need
        } else if(Variables[i].Allocated == 0) {
            //dbp("Size %d set to %d for var '%s'", Variables[i].SizeOfVar, sizeOfVar, name);
            Variables[i].SizeOfVar = sizeOfVar;
        } else {
            Variables[i].SizeOfVar = sizeOfVar;
            if(Variables[i].Allocated >= sizeOfVar) {
            } else {
                Variables[i].Allocated = 0; // Request to reallocate memory of var
            }
        }
        if(addrl) {
            Variables[i].addrl = *addrl;
        }
    } else { // if(sizeOfVar == 0) // if(addrl) { Allocate SRAM }
        if(name[0] == '#') {
            DWORD addr = 0xff;
            if(IsNumber(&name[1])) {
                addr = hobatoi(&name[1]);

                if((addr == 0xff) || (addr == 0)) {
                    dbps("Not a FSR");
                    //Error("Not a FSR");
                } else {
                    if(Variables[i].Allocated == 0) {
                        Variables[i].addrl = addr;
                    }
                    Variables[i].Allocated = 1;
                }
            } else {
                int j = name[strlen(name) - 1] - 'A';
                if((j >= 0) && (j < MAX_IO_PORTS)) {
                    if((strstr(name, "#PORT")) && (strlen(name) == 6)) { // #PORTx
                        if(IS_MCU_REG(j)) {
                            addr = Prog.mcu->outputRegs[j];
                        }
                    }
                    if((strstr(name, "#PIN")) && (strlen(name) == 5)) { // #PINx
                        if(IS_MCU_REG(j)) {
                            addr = Prog.mcu->inputRegs[j];
                        }
                    }
                    if((strstr(name, "#TRIS")) && (strlen(name) == 6)) { // #TRISx
                        if(IS_MCU_REG(j)) {
                            addr = Prog.mcu->dirRegs[j];
                        }
                    }
                }
                /*
                if((addr == 0xff) || (addr == 0)) {
                    return MemForVariable(&name[1], addrl);
                }
                */
                if((addr == 0xff) || (addr == 0)) {
                    //dbps("Not a PORT/PIN");
                    //Error(_("Not a #PORT/#PIN/#TRIS/%s "), name);
                } else {
                    if(Variables[i].Allocated == 0) {
                        Variables[i].addrl = addr;
                    }
                    Variables[i].Allocated = 1;
                }
            }
        } else {
            if((sizeOfVar < 1) || (sizeOfVar > 4)) {
                sizeOfVar = Variables[i].SizeOfVar;
            }
            if((sizeOfVar < 1) || (sizeOfVar > 4)) {
                sizeOfVar = 2;
            }
            if(sizeOfVar < 1) {
                THROW_COMPILER_EXCEPTION_FMT(_("Size of var '%s'(%d) reset as signed 8 bit variable."), name, sizeOfVar);
                sizeOfVar = 1;
            } else if(sizeOfVar > 4) {
                THROW_COMPILER_EXCEPTION_FMT(_("Size of var '%s'(%d) reset as signed 32 bit variable."), name, sizeOfVar);
                sizeOfVar = 4;
            }
            if(Variables[i].SizeOfVar != sizeOfVar) {
                if(!Variables[i].SizeOfVar)
                    Variables[i].SizeOfVar = sizeOfVar;
                else {
                    // Error("no Resize %s %d %d", name, Variables[i].SizeOfVar, sizeOfVar);
                }
            }
            if(addrl) {
                if(Variables[i].Allocated == 0) {
                    if(sizeOfVar == 1) {
                        Variables[i].addrl = AllocOctetRam();
                    } else if(sizeOfVar == 2) {
                        Variables[i].addrl = AllocOctetRam(2);
                    } else if(sizeOfVar == 3) {
                        Variables[i].addrl = AllocOctetRam(3);
                    } else if(sizeOfVar == 4) {
                        Variables[i].addrl = AllocOctetRam(4);
                    } else {
                        THROW_COMPILER_EXCEPTION_FMT(_("Var '%s' not allocated %d."), name, sizeOfVar);
                    }
                    Variables[i].Allocated = sizeOfVar;

                    if(Variables[i].SizeOfVar < sizeOfVar) {
                        //dbp("Err: Allocated '%s', upsize %d set to %d", name, Variables[i].SizeOfVar, sizeOfVar);
                    }

                } else if(Variables[i].Allocated != sizeOfVar) {
                    //Error(" Variable '%s' already allocated as signed %d bit", Variables[i].name, Variables[i].Allocated*8);
                    //CompileError();
                }
            }
        }
        if(addrl)
            *addrl = Variables[i].addrl;
    }
    return Variables[i].SizeOfVar;
}

int MemForVariable(const char *name, DWORD *addr)
{
    return MemForVariable(name, addr, 0);
}

int MemForVariable(const NameArray &name, DWORD *addr)
{
    return MemForVariable(name.c_str(), addr);
}

//-----------------------------------------------------------------------------
int MemOfVar(const char *name, DWORD *addr)
{
    MemForVariable(name, addr, -1); //get WORD memory for pointer to LPM
    return SizeOfVar(name);         //and return size of element of table in flash memory
}

int MemOfVar(const NameArray &name, DWORD *addr)
{
    return MemOfVar(name.c_str(), addr);
}

int SetMemForVariable(const char *name, DWORD addr, int sizeOfVar)
{
    MemForVariable(name, &addr, sizeOfVar); //allocate WORD memory for pointer to LPM

    return MemForVariable(name, nullptr, sizeOfVar); //and set size of element of table in flash memory
}

int SetMemForVariable(const NameArray &name, DWORD addr, int sizeOfVar)
{
    return SetMemForVariable(name.c_str(), addr, sizeOfVar);
}

//-----------------------------------------------------------------------------
int SetSizeOfVar(const char *name, int sizeOfVar, bool showError)
{
    if(showError)
        if((sizeOfVar < 1)/* || (4 < sizeOfVar)*/) {
            Error(_(" Invalid size (%d) of variable '%s' set to 2!"), sizeOfVar, name);
            sizeOfVar = 2;
        }
#ifndef NEW_CMP
    sizeOfVar = 2;
#endif
    return MemForVariable(name, nullptr, sizeOfVar);
}

int SetSizeOfVar(const char *name, int sizeOfVar)
{
    return SetSizeOfVar(name, sizeOfVar, true);
}

int SizeOfVar(const char *name)
{
    if(IsNumber(name))
        return byteNeeded(hobatoi(name));
    else
        return MemForVariable(name, nullptr, 0);
}

int SizeOfVar(const NameArray &name)
{
    return SizeOfVar(name.c_str());
}

//-----------------------------------------------------------------------------
int GetVariableType(char *name)
{
    if(strlenalnum(name) == 0) {
        THROW_COMPILER_EXCEPTION_FMT(_("Empty variable name '%s'.\nrungNow=%d"), name, rungNow + 1);
    }

    int i;
    for(i = 0; i < VariableCount; i++) {
        if(strcmp(name, Variables[i].name) == 0)
            break;
    }
    if(i >= MAX_IO) {
        THROW_COMPILER_EXCEPTION(_("Internal limit exceeded (number of vars)"));
    }
    if(i < VariableCount) {
        return Variables[i].type;
    }
    return IO_TYPE_PENDING;
}

int SetVariableType(const char *name, int type)
{
    if(strlenalnum(name) == 0) {
        THROW_COMPILER_EXCEPTION_FMT(_("Empty variable name '%s'.\nrungNow=%d"), name, rungNow + 1);
    }
    int i;
    for(i = 0; i < VariableCount; i++) {
        if(strcmp(name, Variables[i].name) == 0)
            break;
    }
    if(i >= MAX_IO) {
        THROW_COMPILER_EXCEPTION(_("Internal limit exceeded (number of vars)"));
    }
    if(i == VariableCount) {
        VariableCount++;
        memset(&Variables[i], 0, sizeof(Variables[i]));
        strcpy(Variables[i].name, name);
        if(name[0] == '#') {
            Variables[i].SizeOfVar = 1;
            Variables[i].Allocated = 0;
        } else {
            Variables[i].SizeOfVar = 0;
            Variables[i].Allocated = 0;
        }
        Variables[i].type = type;
    } else {
        if(Variables[i].type == IO_TYPE_PENDING) {
            Variables[i].type = type;
        } else {
            if((Variables[i].type == IO_TYPE_COUNTER) && (type == IO_TYPE_GENERAL)) {
                /*skip*/;
            } else if((Variables[i].type == IO_TYPE_GENERAL) && (type == IO_TYPE_COUNTER)) {
                Variables[i].type = type; // replace // iolist.cpp
            } else {
            }
        }
    }
    return Variables[i].type;
}
//-----------------------------------------------------------------------------

int AllocOfVar(char *name)
{
    if(strlenalnum(name) == 0) {
        THROW_COMPILER_EXCEPTION_FMT(_("Empty variable name '%s'.\nrungNow=%d"), name, rungNow + 1);
    }

    int i;
    for(i = 0; i < VariableCount; i++) {
        if(strcmp(name, Variables[i].name) == 0)
            break;
    }
    if(i >= MAX_IO) {
        THROW_COMPILER_EXCEPTION(_("Internal limit exceeded (number of vars)"));
    }
    if(i < VariableCount) {
        return Variables[i].Allocated;
    }
    return 0;
}

//-----------------------------------------------------------------------------
// Compare function to qsort() the I/O list.
//-----------------------------------------------------------------------------
static int CompareIo(const void *av, const void *bv)
{
    VariablesList *a = (VariablesList *)av;
    VariablesList *b = (VariablesList *)bv;

    return strcmp(a->name, b->name);
}

//-----------------------------------------------------------------------------
void SaveVarListToFile(FILE *f)
{
    qsort(Variables, VariableCount, sizeof(Variables[0]), CompareIo);

    int i;
    for(i = 0; i < VariableCount; i++)
        if(!IsIoType(Variables[i].type) && (Variables[i].type != IO_TYPE_INTERNAL_RELAY)
           && (Variables[i].name[0] != '$')) {
            fprintf(f,
                    "  %3d bytes %s%s\n",
                    SizeOfVar(Variables[i].name),
                    Variables[i].name,
                    Variables[i].Allocated ? "" : " \tNow not used !!!");
        }
}

//-----------------------------------------------------------------------------
bool LoadVarListFromFile(FILE *f)
{
    //ClrInternalData(); // VariableCount = 0;
    //ClrSimulationData();
    AllocStart();
    char line[MAX_NAME_LEN];
    char name[MAX_NAME_LEN];
    int  sizeOfVar;
    bool Ok;

    while(fgets(line, sizeof(line), f)) {
        if(!strlen(strspace(line)))
            continue;
        if(strcmp(line, "END") == 0) {
            return true;
        }
        Ok = false;
        // Don't internationalize this! It's the file format, not UI.
        if(sscanf(line, " %s signed %d bit variable ", name, &sizeOfVar) == 2) {
            if((sizeOfVar > 0) && strlen(name)) {
                SetSizeOfVar(name, sizeOfVar / 8);
                Ok = true;
            }
        }
        if(sscanf(line, " %d bytes %s ", &sizeOfVar, name) == 2) {
            if((sizeOfVar > 0) && strlen(name)) {
                SetSizeOfVar(name, sizeOfVar, false);
                Ok = true;
            }
        }
        if(!Ok) {
            THROW_COMPILER_EXCEPTION_FMT(_("Error reading 'VAR LIST' section from .ld file!\nError in line:\n'%s'."), strspacer(line));
            return false;
        }
    }
    return false;
}
//-----------------------------------------------------------------------------
// Allocate or retrieve the bit of memory assigned to an internal relay or
// other thing that requires a single bit of storage.
//-----------------------------------------------------------------------------
static void MemForBitInternal(const char *name, DWORD *addr, int *bit, bool writeTo)
{
    int i;
    for(i = 0; i < InternalRelayCount; i++) {
        if(strcmp(name, InternalRelays[i].name) == 0)
            break;
    }
    if(i >= MAX_IO) {
        THROW_COMPILER_EXCEPTION(_("Internal limit exceeded (number of relay)"));
    }
    if(i == InternalRelayCount) {
        InternalRelayCount++;
        strcpy(InternalRelays[i].name, name);
        AllocBitRam(&InternalRelays[i].addr, &InternalRelays[i].bit);
        InternalRelays[i].assignedTo = false;
    }

    *addr = InternalRelays[i].addr;
    *bit = InternalRelays[i].bit;
    if(writeTo) {
        InternalRelays[i].assignedTo = true;
    }
}

//-----------------------------------------------------------------------------
// Retrieve the bit to read to determine whether a set of contacts is open
// or closed. Contacts could be internal relay, output pin, or input pin,
// or one of the internal state variables ($xxx) from the int code generator.
//-----------------------------------------------------------------------------
void MemForSingleBit(const char *name, bool forRead, DWORD *addr, int *bit)
{
    *addr = -1;
    *bit = -1;
    if(!(name) || (name && strlen(name) == 0)) {
        return;
    }
    switch(name[0]) {
        case 'I':
        case 'X':
            if(!forRead)
                THROW_COMPILER_EXCEPTION("Internal error");
            MemForPin(name, addr, bit, true);
            break;

        case 'P':
        case 'Y':
            MemForPin(name, addr, bit, false);
            break;

        case 'R':
        case '$':
            MemForBitInternal(name, addr, bit, !forRead);
            break;

        default:
            THROW_COMPILER_EXCEPTION_FMT("Unknown name >%s<", name);
            break;
    }
}

void MemForSingleBit(const char *name, DWORD *addr, int *bit)
{
    MemForSingleBit(name, false, addr, bit);
}

void MemForSingleBit(const NameArray &name, bool forRead, DWORD *addr, int *bit)
{
    MemForSingleBit(name.c_str(), forRead, addr, bit);
}

//-----------------------------------------------------------------------------
int isPinAssigned(const NameArray &name)
{
    int res = 0;
    if((Prog.mcu) && ((Prog.mcu->whichIsa == ISA_AVR) || (Prog.mcu->whichIsa == ISA_PIC16)))
        switch(name[0]) {
            case 'A':
            case 'I':
            case 'X':
            case 'Y': {
                auto assign = std::find_if(Prog.io.assignment,
                                           Prog.io.assignment + Prog.io.count,
                                           [name](const PlcProgramSingleIo &io) { return (name == io.name); });
                if(assign == (Prog.io.assignment + Prog.io.count))
                    THROW_COMPILER_EXCEPTION(_("Can't find right assign."));

                int pin = assign->pin;
                if(name[0] == 'A') {
                    auto info = std::find_if(Prog.mcu->adcInfo,
                                             Prog.mcu->adcInfo + Prog.mcu->adcCount,
                                             [pin](const McuAdcPinInfo &info) { return (info.pin == pin); });
                    if(info != (Prog.mcu->adcInfo + Prog.mcu->adcCount))
                        res = 1;
                } else {
                    auto info = std::find_if(Prog.mcu->pinInfo,
                                             Prog.mcu->pinInfo + Prog.mcu->pinCount,
                                             [pin](const McuIoPinInfo &info) { return (info.pin == pin); });
                    if(info != (Prog.mcu->pinInfo + Prog.mcu->pinCount))
                        res = 1;
                }
                break;
            }

            case 'R':
            case '$':
                //MemForBitInternal(name, addr, bit, !forRead);
                break;

            default:
                //ooops(">%s<", name);
                break;
        }
    return res;
}

//-----------------------------------------------------------------------------
// Retrieve the bit to write to set the state of an output.
//-----------------------------------------------------------------------------
/*
void MemForCoil(char *name, DWORD *addr, int *bit)
{
    switch(name[0]) {
        case 'Y':
            MemForPin(name, addr, bit, false);
            break;

        case 'R':
            MemForBitInternal(name, addr, bit, true);
            break;

        default:
            oops();
            break;
    }
}
*/
//-----------------------------------------------------------------------------
// Do any post-compilation sanity checks necessary.
//-----------------------------------------------------------------------------
void MemCheckForErrorsPostCompile()
{
    int i;
    for(i = 0; i < InternalRelayCount; i++) {
        if(!InternalRelays[i].assignedTo) {
            THROW_COMPILER_EXCEPTION_FMT(_("Internal relay '%s' never assigned; add its coil somewhere."), InternalRelays[i].name);
        }
    }
}

//-----------------------------------------------------------------------------
// From the I/O list, determine which pins are inputs and which pins are
// outputs, and pack that in 8-bit format as we will need to write to the
// TRIS or DDR registers. ADC pins are neither inputs nor outputs.
//-----------------------------------------------------------------------------
void BuildDirectionRegisters(BYTE *isInput, BYTE *isAnsel, BYTE *isOutput, bool raiseError)
{
    if(!Prog.mcu)
        THROW_COMPILER_EXCEPTION(_("Invalid MCU"));

    memset(isOutput, 0x00, MAX_IO_PORTS);
    memset(isAnsel, 0x00, MAX_IO_PORTS);
    memset(isInput, 0x00, MAX_IO_PORTS);

    bool usedUart = UartFunctionUsed();
    int  usedPwm = PwmFunctionUsed();

    int i;
    for(i = 0; i < Prog.io.count; i++) {
        int pin = Prog.io.assignment[i].pin;
        int type = Prog.io.assignment[i].type;

        if(type == IO_TYPE_READ_ADC) {
            auto iop = std::find_if(Prog.mcu->pinInfo,
                                    Prog.mcu->pinInfo + Prog.mcu->pinCount,
                                    [pin](const McuIoPinInfo &pi) { return (pi.pin == pin); });
            if(iop != (Prog.mcu->pinInfo + Prog.mcu->pinCount))
                isAnsel[iop->port - 'A'] |= (1 << iop->bit);
        }

        if(type == IO_TYPE_DIG_OUTPUT || //
           type == IO_TYPE_PWM_OUTPUT || //
           type == IO_TYPE_INT_INPUT ||  //
           type == IO_TYPE_DIG_INPUT) {

            auto iop = std::find_if(Prog.mcu->pinInfo,
                                    Prog.mcu->pinInfo + Prog.mcu->pinCount,
                                    [pin](const McuIoPinInfo &pi) { return (pi.pin == pin); });
            if(iop == (Prog.mcu->pinInfo + Prog.mcu->pinCount)) {
                THROW_COMPILER_EXCEPTION_FMT(_("Must assign pins for all I/O.\r\n\r\n'%s' is not assigned."),
                                             Prog.io.assignment[i].name);
            }
            if((type == IO_TYPE_DIG_OUTPUT) || (type == IO_TYPE_PWM_OUTPUT)) {
                isOutput[iop->port - 'A'] |= (1 << iop->bit);
            } else {
                isInput[iop->port - 'A'] |= (1 << iop->bit);
            }

            if(raiseError) {
                if(usedUart && (pin == Prog.mcu->uartNeeds.rxPin || pin == Prog.mcu->uartNeeds.txPin)) {
                    THROW_COMPILER_EXCEPTION_FMT(_("UART in use; pins %d and %d reserved for that."),
                                                 Prog.mcu->uartNeeds.rxPin,
                                                 Prog.mcu->uartNeeds.txPin);
                }
            }
        }
    }
    if(usedUart) {
        McuIoPinInfo *iop;
        iop = PinInfo(Prog.mcu->uartNeeds.txPin);
        if(iop)
            isOutput[iop->port - 'A'] |= (1 << iop->bit);
        else
            THROW_COMPILER_EXCEPTION(_("Invalid TX pin."));

        iop = PinInfo(Prog.mcu->uartNeeds.rxPin);
        if(iop)
            isInput[iop->port - 'A'] |= (1 << iop->bit);
        else
            THROW_COMPILER_EXCEPTION(_("Invalid RX pin."));
    }
    if(McuAs("Microchip PIC16F877 ")) {
        // This is a nasty special case; one of the extra bits in TRISE
        // enables the PSP, and must be kept clear (set here as will be
        // inverted).
        isOutput[4] |= 0xf8; // TRISE
    }
}

void BuildDirectionRegisters(BYTE *isInput, BYTE *isAnsel, BYTE *isOutput)
{
    BuildDirectionRegisters(isInput, isAnsel, isOutput, true);
}

//-----------------------------------------------------------------------------
// Display our boilerplate warning that the baud rate error is too high.
//-----------------------------------------------------------------------------
void ComplainAboutBaudRateError(int divisor, double actual, double err)
{
    Error(_("UART baud rate generator: divisor=%d actual=%.4f for %.2f%% "
            "error.\r\n"
            "\r\n"
            "This is too large; try a different baud rate (slower "
            "probably), or a crystal frequency chosen to be divisible "
            "by many common baud rates (e.g. 3.6864 MHz, 14.7456 MHz).\r\n"
            "\r\n"
            "Code will be generated anyways but serial may be "
            "unreliable or completely broken."),
          divisor,
          actual,
          err);
}

//-----------------------------------------------------------------------------
// Display our boilerplate warning that the baud rate is too slow (making
// for an overflowed divisor).
//-----------------------------------------------------------------------------
void ComplainAboutBaudRateOverflow()
{
    Error(
        _("UART baud rate generator: too slow, divisor overflows. "
          "Use a slower crystal or a faster baud rate.\r\n"
          "\r\n"
          "Code will be generated anyways but serial will likely be "
          "completely broken."));
}
//-----------------------------------------------------------------------------
/*
    International System (SI) prefixes.
           10 power
yotta   Y    24     septillion      quadrillion
zetta   Z    21     sextillion      thousand trillion
exa     E    18     quintillion     trillion
peta    P    15     quadrillion     thousand billion
tera    T    12     trillion        billion
giga    G    9      billion         thousand million
mega    M    6      million
kilo    k    3      thousand
hecto   h    2      hundred
deca    da   1      ten
             0      one
deci    d   -1      tenth
centi   c   -2      hundredth
milli   m   -3      thousandth
micro   u   -6      millionth
nano    n   -9      billionth       thousand millionth
pico    p   -12     trillionth      billionth
femto   f   -15     quadrillionth   thousand billionth
atto    a   -18     quintillionth   trillionth
zepto   z   -21     sextillionth    thousand trillionth
yocto   y   -24     septillionth    quadrillionth
*/
double SIprefix(double val, char *prefix, int en_1_2)
{
    //dbp("SIprefix val=%f",val);
    if(val >= 1e24) {
        strcpy(prefix, "Y");
        return val / 1e24;
    } else if(val >= 1e21) {
        strcpy(prefix, "Z");
        return val / 1e21;
    } else if(val >= 1e18) {
        strcpy(prefix, "E");
        return val / 1e18;
    } else if(val >= 1e15) {
        strcpy(prefix, "P");
        return val / 1e15;
    } else if(val >= 1e12) {
        strcpy(prefix, "T");
        return val / 1e12;
    } else if(val >= 1e9) {
        strcpy(prefix, "G");
        return val / 1e9;
    } else if(val >= 1e6) {
        strcpy(prefix, "M");
        return val / 1e6;
    } else if(val >= 1e3) {
        strcpy(prefix, "k");
        return val / 1e3;
    } else if((val >= 1e2) && (en_1_2)) {
        strcpy(prefix, "h");
        return val / 1e2;
    } else if((val >= 1e1) && (en_1_2)) {
        strcpy(prefix, "da");
        return val / 1e1;
    } else if(val >= 1.0) {
        strcpy(prefix, "");
        return val;
    } else if(val == 0.0) {
        strcpy(prefix, "");
        return val;
    } else if(val < 1e-21) {
        strcpy(prefix, "y");
        return val * 1e21 * 1e3;
    } else if(val < 1e-18) {
        strcpy(prefix, "z");
        return val * 1e18 * 1e3;
    } else if(val < 1e-15) {
        strcpy(prefix, "a");
        return val * 1e15 * 1e3;
    } else if(val < 1e-12) {
        strcpy(prefix, "f");
        return val * 1e12 * 1e3;
    } else if(val < 1e-9) {
        strcpy(prefix, "p");
        return val * 1e9 * 1e3;
    } else if(val < 1e-6) {
        strcpy(prefix, "n");
        return val * 1e6 * 1e3;
    } else if(val < 1e-3) {
        strcpy(prefix, "u");
        return val * 1e3 * 1e3;
        /**/
    } else if((val <= 1e-2) && en_1_2) {
        strcpy(prefix, "c");
        return val * 1e2;
    } else if((val <= 1e-1) && en_1_2) {
        strcpy(prefix, "d");
        return val * 1e1;
        /**/
    } else if(val < 1.0) {
        strcpy(prefix, "m"); //10 ms= 0.010 s
        return val * 1e3;
    } else {
        THROW_COMPILER_EXCEPTION("Internal error");
        return 0;
    }
}
double SIprefix(double val, char *prefix)
{
    return SIprefix(val, prefix, 0);
}
