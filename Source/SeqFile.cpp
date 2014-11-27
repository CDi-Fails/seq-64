/*
  ==============================================================================

    SeqFile.cpp
    Created: 1 Nov 2014 12:01:52pm
    Author:  Sauraen

  ==============================================================================
*/

#include "SeqFile.h"
#include "ROM.h"

Identifier SeqFile::idName("name");
Identifier SeqFile::idLength("length");
Identifier SeqFile::idAction("action");
Identifier SeqFile::idCmd("cmd");
Identifier SeqFile::idCmdEnd("cmdend");
Identifier SeqFile::idMeaning("meaning");
Identifier SeqFile::idValue("value");
Identifier SeqFile::idAdd("add");
Identifier SeqFile::idMultiply("multiply");
Identifier SeqFile::idDataSrc("datasrc");
Identifier SeqFile::idDataLen("datalen");
Identifier SeqFile::idDataAddr("dataaddr");
Identifier SeqFile::idDataActualLen("dataactuallen");
Identifier SeqFile::idValidInSeq("validinseq");
Identifier SeqFile::idValidInChn("validinchn");
Identifier SeqFile::idValidInTrk("validintrk");


SeqFile::SeqFile(ROM& rom, ValueTree cmdlistnode, uint32 seq_addr, uint32 orig_len){
    cmdlist = cmdlistnode;
    seqaddr = seq_addr;
    length = orig_len;
    if(rom.isByteSwapped && ((seqaddr & 0x00000003) || (length & 0x00000003))){
        DBG("Byte-swapped ROM with non-word-aligned data... this will end poorly!");
    }
    if(length >= 10000000){
        DBG("Trying to load sequence more than 10MB! Crashing to protect your computer!");
        data->getData(); //Dereference null pointer
        return;
    }
    data = new ROM(length, true);
    rom.copyTo(data->getData(), seqaddr, length);
    data->isByteSwapped = rom.isByteSwapped;
    DBG("Copied ROM data to sequence, size == " + ROM::hex((uint32)data->getSize()));
}
SeqFile::SeqFile(ValueTree cmdlistnode){
    cmdlist = cmdlistnode;
    seqaddr = 0;
    length = 0;
    data = new ROM(0, true);
}

SeqFile::~SeqFile(){
    data = nullptr;
}
uint8 SeqFile::readByte(uint32 address){
    return (uint8)(data->readByte(address));
}
void SeqFile::writeByte(uint32 address, uint8 d){
    data->writeByte(address, d);
}


uint32 SeqFile::getStartAddr(){
    return seqaddr;
}
uint32 SeqFile::getLength(){
    return length;
}


//Stype: 0 seq hdr, 1 chn hdr, 2 track data
ValueTree SeqFile::getDescription(uint8 firstbyte, int stype){
    ValueTree test;
    for(int i=0; i<cmdlist.getNumChildren(); i++){
        test = cmdlist.getChild(i);
        if(       (stype == 0 && (bool)test.getProperty(idValidInSeq, false))
                ||(stype == 1 && (bool)test.getProperty(idValidInChn, false))
                ||(stype == 2 && (bool)test.getProperty(idValidInTrk, false))){
            if(test.hasProperty(idCmdEnd)){
                if(firstbyte >= (int)test.getProperty(idCmd)
                        && firstbyte <= (int)test.getProperty(idCmdEnd)){
                    return test;
                }
            }else{
                if(firstbyte == (int)test.getProperty(idCmd)){
                    return test;
                }
            }
        }
    }
    test = ValueTree();
    return test;
}

ValueTree SeqFile::getCommand(uint32 address, int stype){
    ValueTree ret("command");
    ValueTree param, desc;
    String action, meaning, datasrc;
    int i, len, paramlen, paramindex, cmdoffset, paramvalue, datalen;
    uint8 c, d;
    uint32 a = address;
    //
    len = 1;
    c = data->readByte(address);
    a++;
    desc = getDescription(c, stype);
    if(desc.isValid()){
        ret = desc.createCopy();
        cmdoffset = c - (int)ret.getProperty(idCmd, 0);
        action = desc.getProperty(idAction, "No Action");
        //DBG(ROM::hex((uint32)address, 6) + ": " + ROM::hex(c) + " " + action);
        for(paramindex=0; paramindex<ret.getNumChildren(); paramindex++){
            param = ret.getChild(paramindex);
            meaning = param.getProperty(idMeaning, "None");
            //Get the value of this parameter
            paramvalue = 0;
            paramlen = 0;
            datasrc = param.getProperty(idDataSrc, "fixed");
            datalen = param.getProperty(idDataLen, 0);
            if(datasrc == "offset"){
                paramvalue = cmdoffset;
                param.setProperty(idDataAddr, 0, nullptr);
                param.setProperty(idDataActualLen, 1, nullptr);
            }else if(datasrc == "fixed"){
                for(i=0; i<datalen; i++){
                    len++;
                    paramlen++;
                    paramvalue <<= 8;
                    paramvalue += (uint8)data->readByte(a+i);
                }
                param.setProperty(idDataAddr, (int)(a-address), nullptr);
                param.setProperty(idDataActualLen, paramlen, nullptr);
            }else if(datasrc == "variable"){
                if(datalen == 1){
                    d = (uint8)data->readByte(a);
                    if(d <= 0x7F){
                        paramvalue = d;
                        len++;
                        paramlen++;
                    }
                }else if(datalen == 2){
                    d = 0;
                    len++;
                    paramlen++;
                    paramvalue = (uint8)data->readByte(a);
                    if(paramvalue & 0x80){
                        paramvalue &= 0x7F;
                        paramvalue <<= 8;
                        paramvalue += (uint8)data->readByte(a+1);
                        len++;
                        paramlen++;
                    }
                }else{
                    DBG("Due to SeqFile variable length format, length > 2 not defined!");
                    paramvalue = 0;
                    len += datalen;
                    paramlen += datalen;
                    /*
                    while(true){
                        d = (uint8)data->readByte(a+i);
                        paramvalue <<= 7;
                        if(i >= datalen){
                            //Last byte
                            paramvalue += (uint8)d;
                            break;
                        }else{
                            //Intermediate byte
                            paramvalue += (uint8)(d & 0x7F);
                            if(!(d & 0x80)) break;
                        }
                        i++;
                        len++;
                        paramlen++;
                    }
                    */
                }
                param.setProperty(idDataAddr, (int)(a-address), nullptr);
                param.setProperty(idDataActualLen, paramlen, nullptr);
            }else{
                DBG("Invalid command description! datasrc == " + datasrc + ", action == " + action);
            }
            //Store info about parameter
            param.setProperty(idValue, paramvalue, nullptr);
            a += paramlen;
        }
        //Store info about command
        ret.setProperty(idCmd, (int)c, nullptr);
        if(ret.hasProperty(idCmdEnd)){
            ret.removeProperty(idCmdEnd, nullptr);
        }
    }else{
        //Command not found
        ret.setProperty(idCmd, (int)c, nullptr);
    }
    ret.setProperty(idLength, len, nullptr);
    return ret;
}

int SeqFile::getAdjustedValue(const ValueTree& param){
    if(!param.hasProperty(idValue)) return 0;
    int origvalue = (int)param.getProperty(idValue);
    //Add first
    origvalue += (int)param.getProperty(idAdd, 0);
    origvalue = (int)((double)origvalue * (double)param.getProperty(idMultiply, 1.0f));
    return origvalue;
}

SeqData* SeqFile::getOrMakeSectionAt(uint32 a){
    for(int s=0; s<sections.size(); s++){
        if(sections[s]->address == a){
            return sections[s];
        }
    }
    SeqData* newsection = new SeqData();
    newsection->address = a;
    sections.add(newsection);
    return newsection;
}
bool SeqFile::isSectionAt(uint32 a){
    for(int s=0; s<sections.size(); s++){
        if(sections[s]->address == a){
            return true;
        }
    }
    return false;
}
int SeqFile::getNumSections(){
    return sections.size();
}
SeqData* SeqFile::getSection(int s){
    if(s < 0 || s >= sections.size()) return nullptr;
    return sections[s];
}
String SeqFile::getSectionDescription(int s){
    if(s < 0 || s >= sections.size()) return "";
    SeqData* sec = sections[s];
    String ret = "@" + ROM::hex(sec->address, 4) + ": ";
    if(sec->stype == 0){
        ret += "Seq -- --";
    }else if(sec->stype == 1){
        ret += "Chn " + String(sec->channel).paddedLeft(' ', 2) + " --";
    }else{
        ret += "Trk " + String(sec->channel).paddedLeft(' ', 2) + " " + String(sec->layer).paddedLeft(' ', 2);
    }
    ret += " " + String(sec->cmdoffsets.size());
    return ret;
}

String SeqFile::getCommandDescription(int s, int c){
    if(s < 0 || s >= sections.size()) return "";
    String ret = "";
    SeqData* section = sections[s];
    if(c < 0 || c >= section->cmdoffsets.size()) return "";
    uint32 a = section->cmdoffsets[c];
    ValueTree cmd = getCommand(a, section->stype);
    int len = cmd.getProperty(idLength, 1);
    for(int i=0; i<len; i++){
        ret += ROM::hex(data->readByte(a+i)) + " ";
    }
    ret = ret.paddedRight(' ', 15);
    ret = "@" + ROM::hex(a, 4) + ": " + ret;
    ret += cmd.getProperty(idName, "[Unknown Cmd]").toString();
    return ret;
}

void SeqFile::insertSpaceAt(uint32 address, int size){
    //Fix pointers
    SeqData* sec;
    ValueTree command, param;
    String action;
    int c;
    uint32 a, datalen;
    int relvalue;
    for(int s=0; s<sections.size(); s++){
        sec = sections[s];
        if(sec->address >= address){
            sec->address += size;
        }
        for(c=0; c<sec->cmdoffsets.size(); c++){
            a = sec->cmdoffsets[c];
            editCmdPointer(a, sec->stype, address, size);
            if(a >= address){
                sec->cmdoffsets.set(c, a + size);
            }
        }
    }
    //Actually insert space
	Array<char> newdata;
	newdata.resize(size);
    data->insert(newdata.getRawDataPointer(), size, address);
}
void SeqFile::removeData(uint32 address, int size){
    //Fix pointers
    SeqData* sec;
    ValueTree command, param;
    int c;
    uint32 a, oldaddr;
    for(int s=0; s<sections.size(); s++){
        sec = sections[s];
        if(sec->address >= address){
            sec->address -= size;
            if(sec->address < address) sec->address = address;
        }
        for(c=0; c<sec->cmdoffsets.size(); c++){
            a = sec->cmdoffsets[c];
            editCmdPointer(a, sec->stype, address, 0-size);
            if(a >= address){
                a -= size;
                if(a < address) a = address;
                sec->cmdoffsets.set(c, a);
            }
        }
    }
    //Actually delete data
    data->removeSection(address, size);
}

void SeqFile::editCmdPointer(uint32 cmdaddr, int stype, uint32 daddr, int dsize){
    ValueTree command = getCommand(cmdaddr, stype);
    String action = command.getProperty(idAction);
    //Fix absolute pointers
    ValueTree param = command.getChildWithProperty(idMeaning, "Absolute Address");
    if(param.isValid()){
        if(param.getProperty(idDataSrc, "fixed").toString() != "fixed"){
            DBG("Address command parameters must be fixed-length! (in " + action + ")");
            return;
        }
        int oldvalue = (int)param.getProperty(idValue, 0);
        //DBG("Absolute Address value found, " + ROM::hex((uint32)oldvalue,4));
        if(oldvalue >= daddr){
            int newvalue = oldvalue + dsize;
            int datalen = (int)param.getProperty(idDataLen, 1);
            //Check out-of-range
            if(newvalue < 0 || newvalue >= (1 << (datalen << 3))){ //8-bit for one, 16-bit for two...
                DBG("Absolute address pointer going out-of-range @" + ROM::hex(cmdaddr,4) 
                        + " in " + action + ", now " + String(newvalue) + "!");
                return;
            }
            int a = cmdaddr + (int)param.getProperty(idDataAddr, 1);
            for(int i=a+datalen-1; i>=a; i--){
                data->writeByte(i, (newvalue & 0xFF));
                newvalue >>= 8;
            }
        }
    }
    //Fix relative pointers
    param = command.getChildWithProperty(idMeaning, "Relative Address");
    if(param.isValid()){
        if(param.getProperty(idDataSrc, "fixed").toString() != "fixed"){
            DBG("Address command parameters must be fixed-length! (in " + action + ")");
            return;
        }
        int oldvalue = (int)param.getProperty(idValue, 0);
        //DBG("Relative address value @" + ROM::hex(cmdaddr,4) + " parsed to " + String(oldvalue));
        int newvalue = 0;
        int datalen = (int)param.getProperty(idDataLen, 1);
        if(cmdaddr >= daddr && (int)cmdaddr + oldvalue < daddr){
            newvalue = oldvalue - dsize;
        }else if(cmdaddr < daddr && (int)cmdaddr + oldvalue >= daddr){
            newvalue = oldvalue + dsize;
        }else{
            return;
        }
        //Check out-of-range
        int max_value = (1 << ((datalen << 3) - 1)) - 1; //7-bit for one, 15-bit for two...
        if(newvalue < 0 - max_value || newvalue > max_value){
            DBG("Relative address pointer going out-of-range @" + ROM::hex(cmdaddr,4) 
                    + " in " + action + ", now " + String(newvalue) + "!");
            return;
        }
        int a = cmdaddr + (int)param.getProperty(idDataAddr, 1);
        for(int i=a+datalen-1; i>=a; i--){
            data->writeByte(i, (newvalue & 0xFF));
            newvalue >>= 8;
        }
    }
}

int SeqFile::editCmdParam(uint32 address, int stype, String meaning, int newvalue){
    DBG("Editing command parameter @" + ROM::hex(address,4) + " stype " + String(stype) + " " + meaning + " to " + ROM::hex((uint32)newvalue));
    int ret = 0;
    ValueTree command = getCommand(address, stype);
    ValueTree param = command.getChildWithProperty(idMeaning, meaning);
    int value = param.getProperty(idValue, 0);
    if(newvalue == value) return 0;
    if(!param.isValid()){
        DBG("Error: asked to edit command parameter with meaning " + meaning + ", does not exist!");
        return -1;
    }
    uint32 a = address + (int)param.getProperty(idDataAddr, 1);
    String datasrc = param.getProperty(idDataSrc, "fixed");
    int datalen = param.getProperty(idDataLen, 1);
    int dataactuallen = param.getProperty(idDataActualLen, 1);
    if(datasrc == "offset"){
        int cmdbegin = command.getProperty(idCmd, 0);
        int cmdend = command.getProperty(idCmdEnd, 0);
        if(newvalue > (cmdend - cmdbegin) || newvalue < 0) return -1;
        data->writeByte(a, cmdbegin + newvalue);
    }else if(datasrc == "fixed"){
        for(int i=a+datalen-1; i>=a; i--){
            data->writeByte(i, (newvalue & 0xFF));
            newvalue >>= 8;
        }
    }else if(datasrc == "variable"){
        if(newvalue < 0) newvalue = 0;
        if(datalen == 1){
            if(newvalue > 0x7F) newvalue = 0x7F;
            if(value == 0){
                //Make room for value
                insertSpaceAt(a, 1);
                ret = 1;
                data->writeByte(a, newvalue);
            }else{
                if(newvalue == 0){
                    removeData(a, 1);
                    ret = 1;
                }else{
                    data->writeByte(a, newvalue);
                }
            }
        }else if(datalen == 2){
            if(newvalue >= 0x8000) newvalue = 0x8000;
            if(value <= 0x7F && newvalue >= 0x80){
                insertSpaceAt(a+1, 1);
                ret = 1;
            }else if(value >= 0x80 && newvalue <= 0x7F){
                removeData(a+1, 1);
                ret = 1;
            }
            if(newvalue <= 0x7F){
                data->writeByte(a, newvalue);
            }else{
                data->writeByte(a, 0x80 + (newvalue >> 8));
                data->writeByte(a+1, (newvalue & 0xFF));
            }
        }else{
            DBG("Due to SeqFile variable length format, length > 2 not defined!");
            return -1;
        }
    }else{
         DBG("Invalid command description! datasrc == " + datasrc);
         return -1;
    }
    return ret;
}

void SeqFile::parse(){
    sections.clear();
    DBG("Sequence starts with " + ROM::hex(data->readWord(0)) + ROM::hex(data->readWord(4)));
    ValueTree command, param;
    String action, meaning;
    int cmdlen, channel, notelayer, value;
    uint32 a = 0;
    const int stack_size = 8;
    uint32 addrstack[stack_size];
    int stypestack[stack_size];
    SeqData* sectionstack[stack_size];
    uint32 address;
    int stackptr = 0;
    int stype = 0;
    SeqData* sec;
    //Initial section
    sec = getOrMakeSectionAt(0);
    sec->stype = 0;
    //
    DBG("Parsing Sequence");
    bool ended_naturally = false;
    while(a < data->getSize()){
        sec->cmdoffsets.add(a);
        command = getCommand(a, stype);
        cmdlen = (int)command.getProperty(idLength, 1);
        a += cmdlen;
        action = command.getProperty(idAction, "Unknown");
        //Normal actions
        if(action == "Unknown"){
            //do nothing
        }else if(action == "No Action"){
            //do nothing
        }else if(action == "End of Data"){
            if(stackptr == 0){
                ended_naturally = true;
                break; //Done with header
            }
            //Pop return address
            stackptr--;
            //Restore values
            a = addrstack[stackptr];
            stype = stypestack[stackptr];
            sec = sectionstack[stackptr];
        }else if(action == "Timestamp"){
            //do nothing
        }else if(action == "Ptr Channel Header"){
            param = command.getChildWithProperty(idMeaning, "Absolute Address");
            if(param.isValid()){
                address = getAdjustedValue(param);
            }else{
                param = command.getChildWithProperty(idMeaning, "Relative Address");
                if(param.isValid()){
                    address = (int)getAdjustedValue(param) + (int)a;
                }else{
                    DBG("Ptr Channel Header with no address value!");
                    continue;
                }
            }
            if(address >= data->getSize()){
                DBG("@" + ROM::hex(a,4) + ": Pointer off end of sequence to " + ROM::hex(address,4) + ", skipping!");
                continue;
            }
            if(isSectionAt(address)){
                //Already have gone there, skip
                continue;
            }
            param = command.getChildWithProperty(idMeaning, "Channel");
            if(param.isValid()){
                channel = getAdjustedValue(param);
            }
            if(channel >= 16){
                DBG("Ptr Channel Header with channel >= 16!");
                continue;
            }
            addrstack[stackptr] = a;
            stypestack[stackptr] = stype;
            sectionstack[stackptr] = sec;
            stackptr++;
            if(stackptr >= stack_size){
                DBG("FATAL: Stack Overflow SeqFile::parse()!");
                break;
            }
            a = address;
            stype = 1;
            sec = getOrMakeSectionAt(a);
            sec->stype = 1;
            sec->channel = channel;
        }else if(action == "Ptr Loop Start"){
            //do nothing
        }else if(action == "Ptr Track Data"){
            param = command.getChildWithProperty(idMeaning, "Absolute Address");
            if(param.isValid()){
                address = getAdjustedValue(param);
            }else{
                param = command.getChildWithProperty(idMeaning, "Relative Address");
                if(param.isValid()){
                    address = (int)getAdjustedValue(param) + (int)a;
                }else{
                    DBG("Ptr Track Data with no address value!");
                    continue;
                }
            }
            if(address >= data->getSize()){
                DBG("@" + ROM::hex(a,4) + ": Pointer off end of sequence to " + ROM::hex(address,4) + ", skipping!");
                continue;
            }
            if(isSectionAt(address)){
                //Already have gone there, skip
                continue;
            }
            param = command.getChildWithProperty(idMeaning, "Note Layer");
            if(!param.isValid()){
                DBG("Ptr Track Data with no note layer value!");
                continue;
            }
            notelayer = getAdjustedValue(param);
            addrstack[stackptr] = a;
            stypestack[stackptr] = stype;
            sectionstack[stackptr] = sec;
            stackptr++;
            if(stackptr >= stack_size){
                DBG("FATAL: Stack Overflow SeqFile::parse()!");
                break;
            }
            a = address;
            stype = 2;
            sec = getOrMakeSectionAt(a);
            sec->stype = 2;
            sec->channel = channel;
            sec->layer = notelayer;
        }else if(action == "Ptr More Track Data"){
            param = command.getChildWithProperty(idMeaning, "Absolute Address");
            if(param.isValid()){
                address = getAdjustedValue(param);
            }else{
                param = command.getChildWithProperty(idMeaning, "Relative Address");
                if(param.isValid()){
                    address = (int)getAdjustedValue(param) + (int)a;
                }else{
                    DBG("Ptr More Track Data with no address value!");
                    continue;
                }
            }
            if(address >= data->getSize()){
                DBG("@" + ROM::hex(a,4) + ": Pointer off end of sequence to " + ROM::hex(address,4) + ", skipping!");
                continue;
            }
            if(isSectionAt(address)){
                //Already have gone there, skip
                continue;
            }
            addrstack[stackptr] = a;
            stypestack[stackptr] = stype;
            sectionstack[stackptr] = sec;
            stackptr++;
            if(stackptr >= stack_size){
                DBG("FATAL: Stack Overflow SeqFile::parse()!");
                break;
            }
            a = address;
            sec = getOrMakeSectionAt(a);
            sec->stype = stype;
            sec->channel = channel;
            sec->layer = notelayer;
        }else if(action == "Master Volume"){
            //do nothing
        }else if(action == "Tempo"){
            //do nothing
        }else if(action == "Chn Priority"){
            //do nothing
        }else if(action == "Chn Volume"){
            //do nothing
        }else if(action == "Chn Pan"){
            //do nothing
        }else if(action == "Chn Effects"){
            //do nothing
        }else if(action == "Chn Vibrato"){
            //do nothing
        }else if(action == "Chn Pitch Bend"){
            //do nothing
        }else if(action == "Chn Instrument"){
            //do nothing
        }else if(action == "Chn Transpose"){
            //do nothing
        }else if(action == "Layer Transpose"){
            //do nothing
        }else if(action == "Track Note"){
            //do nothing
        }else{
            DBG("Unknown command action " + action + "!");
        }
    }
    if(!ended_naturally){
        DBG("Parsing sequence ran off end! a==" + ROM::hex(a) + ", length==" + ROM::hex((uint32)data->getSize()));
    }
}


MidiFile* SeqFile::toMIDIFile(){
    ValueTree command, param;
    String action, meaning;
    int channel, notelayer, value, transpose, delay, note, velocity, gate;
    bool qDelay, qVelocity, qGate;
    uint32 t;
    uint32 a = 0;
    const int stack_size = 8;
    uint32 addrstack[stack_size];
    uint32 timestack[stack_size];
    int stypestack[stack_size];
    uint32 address;
    int stackptr = 0;
    int stype = 0;
    uint32 seqhdr_addr = 0;
    Array<uint32> seqhdr_times;
    int max_layers = 4;
	Array<int> transposes;
	transposes.resize(16 * max_layers);
    //
    int ticks_multiplier = (int)cmdlist.getProperty("ppqnmultiplier", 1);
    if(ticks_multiplier <= 0) ticks_multiplier = 1;
    int bend_range = (int)cmdlist.getProperty("bendrange", 2);
    if(bend_range < 0) bend_range = 2;
    String chnvol = cmdlist.getProperty("chnvol", "CC7 (Volume)").toString();
    String mtrvol = cmdlist.getProperty("mtrvol", "CC24 (None)").toString();
    String chnpriority = cmdlist.getProperty("chnpriority", "CC25 (None)").toString();
    const int midi_basenote = 21;
    //
    MidiMessage msg;
    MidiFile* ret = new MidiFile();
    ret->setTicksPerQuarterNote(48 * ticks_multiplier);
    MidiMessageSequence mastertrack;
    OwnedArray<MidiMessageSequence> mtracks;
    for(channel=0; channel<16; channel++){
        mtracks.add(new MidiMessageSequence());
        //Pitch bend range RPN
        msg = MidiMessage::controllerEvent(channel+1, 101, 0);
        msg.setTimeStamp(0);
        mtracks[channel]->addEvent(msg);
        msg = MidiMessage::controllerEvent(channel+1, 100, 0);
        msg.setTimeStamp(0);
        mtracks[channel]->addEvent(msg);
        msg = MidiMessage::controllerEvent(channel+1, 6, bend_range);
        msg.setTimeStamp(0);
        mtracks[channel]->addEvent(msg);
        msg = MidiMessage::controllerEvent(channel+1, 38, 0);
        msg.setTimeStamp(0);
        mtracks[channel]->addEvent(msg);
    }
    //
    DBG("EXPORTING MIDI FILE");
    t = 0;
    bool ended_naturally = false;
    while(a < data->getSize()){
        command = getCommand(a, stype);
        a += (int)command.getProperty(idLength, 1);
        action = command.getProperty(idAction, "Unknown");
        //Sequence header command times
        if(stype == 0){
            for(; seqhdr_addr<a; seqhdr_addr++){
                seqhdr_times.add(t);
            }
        }
        //Pre-delay
        param = command.getChildWithProperty(idMeaning, "Pre-Delay");
        if(param.isValid()){
            delay = getAdjustedValue(param);
            t += delay;
        }
        //Normal actions
        if(action == "Unknown"){
            //do nothing
            DBG("Unknown Action " + ROM::hex((uint8)(int)command.getProperty(idCmd)) 
                    + " in stype " + String(stype) + " @" + ROM::hex(a, 6));
        }else if(action == "No Action"){
            //do nothing
            //DBG("No Action " + ROM::hex((uint8)(int)command.getProperty(idCmd)) 
            //        + " in stype " + String(stype) + " @" + ROM::hex(a, 6));
        }else if(action == "End of Data"){
            if(stackptr == 0){
                ended_naturally = true;
                break; //Done with header
            }
            //Pop return address
            stackptr--;
            //Restore values
            a = addrstack[stackptr];
            if(stypestack[stackptr] < stype){
                t = timestack[stackptr];
            }
            stype = stypestack[stackptr];
        }else if(action == "Timestamp"){
            //do nothing, taken care of by pre- and post-delays
        }else if(action == "Ptr Channel Header"){
            param = command.getChildWithProperty(idMeaning, "Absolute Address");
            if(param.isValid()){
                address = getAdjustedValue(param);
            }else{
                param = command.getChildWithProperty(idMeaning, "Relative Address");
                if(param.isValid()){
                    address = (int)getAdjustedValue(param) + (int)a;
                }else{
                    DBG("Ptr Channel Header with no address value!");
                    continue;
                }
            }
            if(address >= data->getSize()){
                DBG("@" + ROM::hex(a,4) + ": Pointer off end of sequence to " + ROM::hex(address,4) + ", skipping!");
                continue;
            }
            param = command.getChildWithProperty(idMeaning, "Channel");
            if(param.isValid()){
                channel = getAdjustedValue(param);
            }
            if(channel >= 16){
                DBG("Ptr Channel Header with channel >= 16!");
                continue;
            }
            addrstack[stackptr] = a;
            timestack[stackptr] = t;
            stypestack[stackptr] = stype;
            stackptr++;
            if(stackptr >= stack_size){
                DBG("FATAL: Stack Overflow SeqFile::toMIDIFile()!");
                break;
            }
            a = address;
            stype = 1;
            velocity = 127;
            gate = 0xFF;
            //DBG("----T" + ROM::hex(t, 6) + ": Entering Chan " + String(channel) + " Hdr");
        }else if(action == "Ptr Loop Start"){
            param = command.getChildWithProperty(idMeaning, "Absolute Address");
            if(param.isValid()){
                address = getAdjustedValue(param);
            }else{
                param = command.getChildWithProperty(idMeaning, "Relative Address");
                if(param.isValid()){
                    address = (int)getAdjustedValue(param) + (int)a;
                }else{
                    DBG("Ptr Loop Start with no address value!");
                    continue;
                }
            }
            if(address >= data->getSize()){
                DBG("@" + ROM::hex(a,4) + ": Pointer off end of sequence to " + ROM::hex(address,4) + ", skipping!");
                continue;
            }
            if(address >= seqhdr_times.size()){
                DBG("Ptr Loop Start points to @" + ROM::hex((uint32)address, 4) + ", unknown time!");
                continue;
            }
            //Make loopEnd marker
            msg = MidiMessage::textMetaEvent(0x06, "loopEnd");
            msg.setTimeStamp(t*ticks_multiplier);
            mastertrack.addEvent(msg);
            //Make loopStart marker
            msg = MidiMessage::textMetaEvent(0x06, "loopStart");
            value = seqhdr_times[address];
            //DBG("Ptr Loop Start t" + ROM::hex((uint32)t, 4) + " to @" + ROM::hex((uint32)address, 4) + ", t" + ROM::hex((uint32)value, 4));
            msg.setTimeStamp(value*ticks_multiplier);
            mastertrack.addEvent(msg);
        }else if(action == "Ptr Track Data"){
            param = command.getChildWithProperty(idMeaning, "Absolute Address");
            if(param.isValid()){
                address = getAdjustedValue(param);
            }else{
                param = command.getChildWithProperty(idMeaning, "Relative Address");
                if(param.isValid()){
                    address = (int)getAdjustedValue(param) + (int)a;
                }else{
                    DBG("Ptr Track Data with no address value!");
                    continue;
                }
            }
            if(address >= data->getSize()){
                DBG("@" + ROM::hex(a,4) + ": Pointer off end of sequence to " + ROM::hex(address,4) + ", skipping!");
                continue;
            }
            param = command.getChildWithProperty(idMeaning, "Note Layer");
            if(!param.isValid()){
                DBG("Ptr Track Data with no note layer value!");
                continue;
            }
            notelayer = getAdjustedValue(param);
            addrstack[stackptr] = a;
            timestack[stackptr] = t;
            stypestack[stackptr] = stype;
            stackptr++;
            if(stackptr >= stack_size){
                DBG("FATAL: Stack Overflow SeqFile::toMIDIFile()!");
                break;
            }
            a = address;
            stype = 2;
            //DBG("----====T" + ROM::hex(t, 6) + ": Entering Track layer " + String(notelayer));
            //DBG("@" + ROM::hex(a, 6) + ": Track Data " + String(notelayer));
        }else if(action == "Ptr More Track Data"){
            param = command.getChildWithProperty(idMeaning, "Absolute Address");
            if(param.isValid()){
                address = getAdjustedValue(param);
            }else{
                param = command.getChildWithProperty(idMeaning, "Relative Address");
                if(param.isValid()){
                    address = (int)getAdjustedValue(param) + (int)a;
                }else{
                    DBG("Ptr More Track Data with no address value!");
                    continue;
                }
            }
            if(address >= data->getSize()){
                DBG("@" + ROM::hex(a,4) + ": Pointer off end of sequence to " + ROM::hex(address,4) + ", skipping!");
                continue;
            }
            addrstack[stackptr] = a;
            timestack[stackptr] = 100000; //This value should never be popped!
            stypestack[stackptr] = stype;
            stackptr++;
            if(stackptr >= stack_size){
                DBG("FATAL: Stack Overflow SeqFile::toMIDIFile()!");
                break;
            }
            a = address;
        }else if(action == "Master Volume"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Master Volume event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            if(value > 0x7F) value = 0x7F;
            if(value < 0) value = 0;
            int cc = -1;
            if(mtrvol == "CC7 (Volume)"){
                cc = 7;
            }else if(mtrvol == "CC11 (Expr)"){
                cc = 11;
            }else if(mtrvol == "CC16 (GPC1)"){
                cc = 16;
            }else if(mtrvol == "CC24 (None)"){
                cc = 24;
            }else if(mtrvol == "SysEx MstrVol"){
                char sysexdata[6];
                sysexdata[0] = 0x7F;
                sysexdata[1] = 0x7F;
                sysexdata[2] = 0x04;
                sysexdata[3] = 0x01;
                sysexdata[4] = 0x7F;
                sysexdata[5] = value;
                msg = MidiMessage::createSysExMessage(sysexdata, 6);
                msg.setTimeStamp(t*ticks_multiplier);
                mastertrack.addEvent(msg);
                continue;
            }else{
                DBG("Master Volume event, unknown mapping: " + mtrvol + ", ignoring");
                continue;
            }
            if(stype == 0){
                for(channel=0; channel<16; channel++){
                    msg = MidiMessage::controllerEvent(channel+1, cc, value);
                    msg.setTimeStamp(t*ticks_multiplier);
                    mtracks[channel]->addEvent(msg);
                }
                channel = 0;
            }else{
                msg = MidiMessage::controllerEvent(channel+1, cc, value);
                msg.setTimeStamp(t*ticks_multiplier);
                mtracks[channel]->addEvent(msg);
            }
        }else if(action == "Tempo"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Tempo event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            uint32 tempovalue = 60000000 / value;
            msg = MidiMessage::tempoMetaEvent(tempovalue);
            msg.setTimeStamp(t*ticks_multiplier);
            mastertrack.addEvent(msg);
        }else if(action == "Chn Priority"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Chn Priority event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            int cc = 25;
            if(chnpriority == "CC17 (GPC2)"){
                cc = 17;
            }else if(chnpriority == "CC25 (None)"){
                cc = 25;
            }else if(chnpriority == "CC79 (SC10)"){
                cc = 79;
            }else{
                DBG("Channel Priority event, unknown mapping: " + chnpriority + ", ignoring");
                continue;
            }
            msg = MidiMessage::controllerEvent(channel+1, cc, value);
            msg.setTimeStamp(t*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
        }else if(action == "Chn Volume"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Chn Volume event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            int cc = 7;
            if(chnvol == "CC7 (Volume)"){
                cc = 7;
            }else if(chnvol == "CC11 (Expr)"){
                cc = 11;
            }else{
                DBG("Channel Volume event, unknown mapping: " + chnvol + ", ignoring");
                continue;
            }
            msg = MidiMessage::controllerEvent(channel+1, cc, value);
            msg.setTimeStamp(t*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
        }else if(action == "Chn Pan"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Chn Pan event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            msg = MidiMessage::controllerEvent(channel+1, 10, value);
            msg.setTimeStamp(t*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
        }else if(action == "Chn Effects"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Chn Effects event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            msg = MidiMessage::controllerEvent(channel+1, 91, value);
            msg.setTimeStamp(t*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
        }else if(action == "Chn Vibrato"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Chn Vibrato event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            msg = MidiMessage::controllerEvent(channel+1, 77, value);
            msg.setTimeStamp(t*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
        }else if(action == "Chn Pitch Bend"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Chn Pitch Bend event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            if(value >= 0x80) value -= 0x100;
            value = (1<<13) + (value << 7);
            if(value < 0) value = 0;
            if(value >= (1<<14)) value = (1<<14) - 1;
            //DBG("Pitch Bend original value " + String(value) + " or " + ROM::hex((uint32)value));
            msg = MidiMessage::pitchWheel(channel+1, value);
            msg.setTimeStamp(t*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
        }else if(action == "Chn Transpose"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Chn Transpose event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            if(value >= 0x80) value -= 0x100;
            for(int i = 0; i < max_layers; i++){
                transposes.set((channel*max_layers)+i, value);
            }
        }else if(action == "Layer Transpose"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Layer Transpose event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            if(value >= 0x80) value -= 0x100;
            transposes.set((channel*max_layers)+notelayer, value);
        }else if(action == "Chn Instrument"){
            param = command.getChildWithProperty(idMeaning, "Value");
            if(!param.isValid()){
                DBG("Chn Instrument event with no value!");
                continue;
            }
            value = getAdjustedValue(param);
            //DBG("Chn Instrument " + String(value) + " channel " + String(channel));
            msg = MidiMessage::programChange(channel+1, value); //TODO original instrument number
            msg.setTimeStamp(t*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
        }else if(action == "Track Note"){
            qDelay = qVelocity = qGate = false;
            //Delay already taken care of
            //Note
            param = command.getChildWithProperty(idMeaning, "Note");
            if(!param.isValid()){
                DBG("Track Note event with no note!");
                continue;
            }
            value = getAdjustedValue(param);
            transpose = transposes[(channel*max_layers)+notelayer];
            note = value + transpose + midi_basenote;
            if(note < 0 || note >= 128){
                DBG("Bad (transposed?) note @" + ROM::hex(a, 4)
                        + ": c " + String(channel) + ", l " + String(notelayer)
                        + ": note " + String(value)
                        + ", transpose " + String(transpose)
                        + ", base " + String(midi_basenote)
                        + ": result " + String(note));
                continue;
            }
            //Velocity
            param = command.getChildWithProperty(idMeaning, "Velocity");
            if(param.isValid()){
                velocity = getAdjustedValue(param);
                qVelocity = true;
            }
            //Gate time
            param = command.getChildWithProperty(idMeaning, "Gate Time");
            if(param.isValid()){
                gate = getAdjustedValue(param);
                qGate = true;
            }else{
                gate = 0;
            }
            //Fetch post-delay to get gate time proportion
            param = command.getChildWithProperty(idMeaning, "Post-Delay");
            if(param.isValid()){
                delay = getAdjustedValue(param);
                qDelay = true;
            }else{
                //DBG("@" + ROM::hex(a, 6) + ": No delay value given, using current " + ROM::hex((uint32)delay, 4));
                //Add it so we actually do the delay!
                param = ValueTree("parameter");
                param.setProperty(idMeaning, "Post-Delay", nullptr);
                param.setProperty(idValue, delay, nullptr);
                command.addChild(param, command.getNumChildren(), nullptr);
            }
            /*
            DBG("@" + ROM::hex(a, 4) + " c " + ROM::hex((uint8)channel, 1) + " l " + ROM::hex((uint8)notelayer, 1)
                    + " n " + ROM::hex((uint8)note)       + " v " +  ROM::hex((uint8)velocity) 
                    + " g " +  ROM::hex((uint32)gate, 4)  + " d " + ROM::hex((uint32)delay, 4)
                    + " r " + String((float)((gate*delay) / 256.0))
                    + (qDelay ? ("") : (" (using old delay)"))
                    + (qGate ? ("") : (" (no gate)"))
                    + (qVelocity ? ("") : (" (using old velocity)"))  );
            */
            msg = MidiMessage::noteOn(channel+1, note, (uint8)velocity);
            msg.setTimeStamp(t*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
            msg = MidiMessage::noteOff(channel+1, note, 0);
            msg.setTimeStamp((t + delay - ((gate*delay) >> 8))*ticks_multiplier);
            mtracks[channel]->addEvent(msg);
        }else{
            DBG("Unknown command action " + action + "!");
        }
        //Post-delay
        param = command.getChildWithProperty(idMeaning, "Post-Delay");
        if(param.isValid()){
            delay = getAdjustedValue(param);
            t += delay;
        }
    }
    if(!ended_naturally){
        DBG("Converting sequence ran off end! a==" + ROM::hex(a) + ", length==" + ROM::hex((uint32)data->getSize()));
    }
    DBG("====== DONE ======");
    ret->addTrack(mastertrack);
    for(channel=0; channel<16; channel++){
        mtracks[channel]->updateMatchedPairs();
        ret->addTrack(*mtracks[channel]);
    }
    return ret;
}


