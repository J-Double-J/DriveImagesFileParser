//
//  main.cpp
//  p7
//
//  Created by Joshua Jacobs on 12/1/21.
//

#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <bitset>
#include <string>
#include <map>
#include <vector>

using namespace std;

#define BSIZE 512    // Block size in bytes

struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Special device

struct stat {
  short type;  // Type of file
  int dev;     // Device number
  uint ino;    // Inode number on device
  short nlink; // Number of links to file
  uint size;   // Size of file in bytes
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

#define DIRSIZ 14

struct dirent {
    ushort inum;
    char name[DIRSIZ];
};

struct FS {
    int fd;
    superblock sb;
    dinode* inodeArray;
    char* bm;
    uint startData;
    
    void checkSystem();
    void readInodes();
    void readBM();
    void DirectoryChecker();
    void memMaintenance();
    
    private:
        map<int, int> expectedLinks;
        map<int, int> actualLinks;
        map<int, int> dataBlockOccurence;
    
        bool isBitMapGood();
        bool isRootValid();
        void BuildDir (const int, const int, map<int, int>);
        void dirsAreCorrect(vector<dirent>&, const int&, const int&);
        bool twoDirDotsCorrect(vector<dirent>&, const int&, const int&);
        bool haveAsciiTitles(vector<dirent>&);
        bool isValidFileSize(dinode&, const int&);
        bool doExpectedMatchActualLinks();
        bool dataBlockChecker();
        void dataBlockOccurred(const int&);

};

#define IPB           (BSIZE / sizeof(struct dinode))


void initFSCK(const char*);
superblock readSB(int&);

int main(int argc, const char * argv[]) {
    if (argc == 2){
        initFSCK((argv[1]));}
    else
        cout << "No File Specified. \n";
    return 0;
}

void initFSCK(const char* s) {
    FS fs;
    int fd;
    try {
        fd = open(s, O_RDONLY, 0644);
        if(fd < 0)
            throw errno;
        fs.fd = fd;
    } catch (...) {
        cout << "Unable to open file. Error: " << fd << endl;
        exit(1);
    }
    
    fs.sb = readSB(fd);
    fs.startData = fs.sb.bmapstart+1;
    fs.readInodes();
    fs.readBM();
    fs.DirectoryChecker();
    fs.memMaintenance();
    
    cout << "No errors detected." << endl;
}

superblock readSB (int& fd) {
    superblock sb;
    lseek(fd, BSIZE, SEEK_SET);
    read(fd, &sb, sizeof(sb));
    
    cout << "Size: " << sb.size << endl;
    cout << "Data Blocks: " << sb.nblocks << endl;
    cout << "Inode Blocks: " << sb.ninodes << endl;
    cout << "Log Blocks: " << sb.nlog << endl;
    cout << "Log Block Start: " << sb.logstart << endl;
    cout << "Inode Start: " << sb.inodestart << endl;
    cout << "Bit Map Start: " << sb.bmapstart << endl;

    
    return sb;
}

//Do in one gulp
void FS::readInodes () {
    
    inodeArray = new dinode [sb.ninodes];
    lseek(fd, BSIZE*sb.inodestart, SEEK_SET);
    read(fd, inodeArray, sizeof(dinode)*sb.ninodes);
    
    
    dirent dire;
    lseek(fd, (inodeArray+1)->addrs[0]*BSIZE, SEEK_SET);
    for (int i = 0; i < 512; i += 16) {
        read(fd, &dire, sizeof(dirent));
    }
}

void FS::readBM() {
    bm = new char [BSIZE];
    lseek(fd, sb.bmapstart*BSIZE, SEEK_SET);
    read(fd, bm, BSIZE);
}

bool FS::isBitMapGood() {
    bool b = true;

    //Loop for every data block
    for (int i = 0; i < sb.nblocks; i++) {
        bitset<8> bs(bm[i/8]); //Whatever datablock we are looking for, divide by 8 to get the char in the bm array
        b = bs[i%8];  //Find the bit location in the char
        cout << b;
        if (dataBlockOccurence[i] != b) {
            if (b) {
                cout << "ERROR: Missing Block " << i << endl;
                exit(1);
            } else {
                cout << "ERROR: Unallocated Block " << i << endl;
                exit(1);
            }
        }
        
    }
    return b;
}


void FS::DirectoryChecker() {
    map<int, int> placeholder;      //Used for BuildDir
    
    if(!isRootValid()){
        cout << "ERROR! Root is improperly set up." << endl;
    }
    
    BuildDir(1, 1, placeholder);
    
    //Check if link count is correct
    if (!doExpectedMatchActualLinks())
        exit(1);
    
    if(!isBitMapGood())
        exit(1);
    
}

bool FS::isRootValid() {
    bool ret = true;
    if ((inodeArray+1)->type == T_DIR) {
        dirent dir;
        lseek(fd, ((inodeArray+1)->addrs[0])*BSIZE, SEEK_SET);
        
        read(fd, &dir, sizeof(dirent));
        if (dir.inum != 1) {
            cout << "ERROR: First inode is not root." << endl;
        } else if (dir.name[0] != '.') {
            cout << "ERROR: Root does not refer to itself with '.' ." << endl;
            ret = false;
        } else {
            read(fd, &dir, sizeof(dirent));
            if(dir.inum  != 1 || strcmp(dir.name, "..") != 0) {
                cout << "ERROR: Root's parent does not refer to itself correctly." << endl;
                ret = false;
            }
        }
    }
    return ret;
}

void FS::BuildDir (const int inodeBlockNum, const int parent, map<int, int> parentMap) {
    parentMap[inodeBlockNum]++;
    if (parentMap[inodeBlockNum] > 1) {
        cout << "ERROR: Directory Loop! Block " << parent << " leads back to inode " << inodeBlockNum << endl;
        exit(1);
    }
    
    //Check if file size is correct of the directory we're in.
    if(isValidFileSize(*(inodeArray+inodeBlockNum), parent) == 0)
        exit(1);
    
    //read linked files
    if((inodeArray+inodeBlockNum)->type == T_DIR){
        vector<dirent> foundDirs;
        
        //Redundant, but it does easily cover root case, instead of ifs
        expectedLinks[inodeBlockNum] = (inodeArray+inodeBlockNum)->nlink;
        
        lseek(fd, (inodeArray+inodeBlockNum)->addrs[0]*BSIZE, SEEK_SET);
        
        for (int i = 0; i < 13; i++) {
            foundDirs.resize(32*(i+1));
            if ((inodeArray+inodeBlockNum)->addrs[i] != 0) {
                dirent dire;
                for (int s = 0; s < BSIZE/sizeof(dirent); s++){
                    read(fd, &dire, sizeof(dirent));
                    //dirent temp = dire;
                    foundDirs.at(s) = dire;
                }
                dataBlockOccurred((inodeArray+inodeBlockNum)->addrs[i]);
                
            } else
                break;
            if (i == NDIRECT) {
                //read indirect block
                if(NDIRECT*BSIZE < (inodeArray+inodeBlockNum)->size) {
                    uint extraDirsCount = 0;    //Tracks the number of addresses in the indirect block
                    lseek(fd, (inodeArray+inodeBlockNum)->addrs[NDIRECT]*BSIZE, SEEK_SET);
                    dataBlockOccurred((inodeArray+inodeBlockNum)->addrs[NDIRECT]);
                    
                    uint temp = 0;
                    for (int j = 0; j < NINDIRECT; j++) {
                        extraDirsCount++;
                        read(fd, &temp, sizeof(uint));
                        if (temp == 0) {               //if 0 means no addresses left
                            extraDirsCount = temp - 1; //has to be at least >0 since there was to be one in the indirect block
                            break;
                        }
                    }
                    
                    lseek(fd, (inodeArray+inodeBlockNum)->addrs[12]*BSIZE, SEEK_SET);
                    read(fd, &temp, sizeof(uint)); //read 1st block address of indirect block
                    lseek(fd, temp*BSIZE, SEEK_SET);
                    
                    assert(extraDirsCount != 0);
                    
                    foundDirs.resize(32*((i+1)+(extraDirsCount)));
                    for (int k = 0; k < extraDirsCount; k++) {
                        dirent di;
                        for (int l = 0; l < BSIZE/sizeof(dirent); l++) {
                            read(fd, &di, sizeof(dirent));
                            foundDirs.at((32*(i+1+extraDirsCount))+l) = di;
                        }
                        dataBlockOccurred(temp+k);
                    }
                }
            }
        }
        
        dirsAreCorrect(foundDirs, parent, inodeBlockNum);
        
        //Recurse through the other directories, skipping reference to self and parent
        for(int i = 2; i < foundDirs.size(); i++){
            actualLinks[foundDirs.at(i).inum]++;
            expectedLinks[foundDirs.at(i).inum] = (inodeArray+foundDirs.at(i).inum)->nlink;
            
            if((inodeArray+foundDirs.at(i).inum)->type == T_DIR) {
                BuildDir(foundDirs.at(i).inum, inodeBlockNum, parentMap);
            } else if ((inodeArray+foundDirs.at(i).inum)->type == T_FILE) {
                //If we're pointing to a file object that is supposed to have more links than 1, it means
                //it has that special link case, so on future finds we just increment nlink, but don't
                //recount all it's datablocks since it would be double counting
                if((inodeArray+foundDirs.at(i).inum)->nlink > 1 && actualLinks[foundDirs.at(i).inum] > 1) {
                } else {
                    BuildDir(foundDirs.at(i).inum, inodeBlockNum, parentMap);
                }
            }
        }
        
        
    } else if ((inodeArray+inodeBlockNum)->type == T_FILE) {
        for (int i = 0; i < NDIRECT; i++) {
            if ((inodeArray+inodeBlockNum)->addrs[i] != 0) {
                dataBlockOccurred((inodeArray+inodeBlockNum)->addrs[i]);
            } else
                break;
            if (i == NDIRECT-1) {
                if(BSIZE*NDIRECT < (inodeArray+inodeBlockNum)->size) {
                    lseek(fd, (inodeArray+inodeBlockNum)->addrs[NDIRECT]*BSIZE, SEEK_SET);
                    dataBlockOccurred((inodeArray+inodeBlockNum)->addrs[NDIRECT]);
                    
                    uint extraInodesFound = 0;
                    uint temp;
                    uint tempfirst = 0;         //Keep track where first inode in indirect block is
                    for (int j = 0; j < 128; j++) {
                        extraInodesFound++;
                        read(fd, &temp, sizeof(uint));
                        if(temp == 0) {
                            extraInodesFound--;
                            break;
                        }
                        if (j == 0) {
                            tempfirst = temp;
                        }
                    }
                    
                    assert(extraInodesFound != 0);
                    assert(tempfirst != 0);
                    
                    for (int k = 0; k < extraInodesFound; k++) {
                        dataBlockOccurred(tempfirst+k);
                    }
                }
            }
        }
        
        
    } else if ((inodeArray+inodeBlockNum)->type == 0) {
        cout << "ERROR: Missing Inode " << inodeBlockNum << endl;
        exit(1);
    } else {
        cout <<"SHOULD NOT BE HERE!" << endl;
        exit(1);
    }
}

//Checks to see if the directory files are correctly set up, right now it checks to see if . and ..
//are correct for first two, but in case I want to do other checks, this is here.
void FS::dirsAreCorrect(vector<dirent>& dirs, const int& PARENT, const int& INUM) {
    for (int i = 0; i < dirs.size(); i++) {
        if (dirs.at(i).inum != 0) {
            if (i == 0) {
                if(twoDirDotsCorrect(dirs, PARENT, INUM))
                    i = 1; //After this iteration, next loop will start at 2
                else
                    exit(1);
            }
        } else {
            //No more valid dirents to check
            break;
        }
    }
    
    if(!haveAsciiTitles(dirs))
        exit(1);
}

//Checks if the first two dir entries are correctly set up
bool FS::twoDirDotsCorrect(vector<dirent>& dirs, const int& PARENT, const int& INUM) {
    bool retval = true;
    if (dirs.at(0).name[0] != '.' ) {
        retval = false;
        cout << "ERROR: Inode " << INUM << " does not reference itself correctly regarding '.'" << endl;
    }
    
    if (dirs.at(0).inum != INUM) {
        retval = false;
        cout << "ERROR: Inode " << INUM << " has . reference not reference itself." << endl;
    }
    
    //Correctly links to itself
    if (retval)
        actualLinks[INUM]++;
    
    
    if (strcmp(dirs.at(1).name, "..") != 0) {
        retval = false;
        cout << "ERROR: Inode " << INUM << " does not reference parent correctly regarding '..'" << endl;
        
    }
    
    if (dirs.at(1).inum != PARENT) {
        retval = false;
        cout << "ERROR: Block " << INUM << " does not reference its parent correctly! Block " << dirs.at(1).inum << "'s parent should be " << PARENT << " and is " << endl;
    }
    
    //Correctly links to parent
    if (retval)
        actualLinks[PARENT]++;
    
    return retval;
}

//Checks to make sure the dirs have typeable
bool FS::haveAsciiTitles(vector<dirent>& dirs) {
    bool retval = true;
    for (int i = 2; i < dirs.size(); i++) {
        for (int j = 0; j < DIRSIZ; j++) {
            if(dirs.at(i).name[j] == 0) {
                break;
            }
            if (dirs.at(i).name[j] < 32 || dirs.at(i).name[j] > 126 || dirs.at(i).name[j] == 47) {
                retval = false;
                cout << "ERROR: Inode " << dirs.at(i).inum << " has a non-ASCII valid dir name: " << dirs.at(i).name << endl;
            }
        }
    }
    return retval;
}

bool FS::isValidFileSize(dinode& di, const int& inum) {
    bool retval = true;
    if (di.type == 0)
        return retval;
    if(di.size > BSIZE*12+(BSIZE*128)) {
        cout << "ERROR: Inode " << inum << " has an invalid size of " << di.size << " - Max is " << BSIZE*12+(BSIZE*128) << endl;
        retval = false;
    }
    return retval;
}

bool FS::doExpectedMatchActualLinks() {
    bool retval = true;
    
    for (int i = 0; i < sb.ninodes; i++) {
        if (actualLinks[i] != expectedLinks[i]) {
            cout << "ERROR: Inode " << i << " has " << actualLinks[i] << " links, when " << expectedLinks[i] << " were expected!" << endl;
            retval = false;
        }
        
        if (actualLinks[i] == 0 && retval == true) {
            if((inodeArray+i)->type != 0) {
                cout << "ERROR: Unused Inode: " << i << "." << endl;
            }
        }
        
        if (retval == false)
            break;
    }
    
    return retval;
}

void FS::dataBlockOccurred(const int& dnum) {
    dataBlockOccurence[dnum]++;
    if (dataBlockOccurence[dnum] > 1) {
        cout << "ERROR: Multiply Allocated Block: " << dnum << endl;
        exit(1);
    }
}

bool FS::dataBlockChecker() {
    bool retval = true;
    
    return retval;
}

void FS::memMaintenance() {
    delete [] bm;
    delete [] inodeArray;
}
