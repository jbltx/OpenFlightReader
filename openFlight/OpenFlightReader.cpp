
#include "Document.h"
#include <fstream>
#include <iostream>
#include "OpCode.h"
#include "OpenFlightReader.h"
#include "Records.h"
#include <regex>
#include <sstream>
#include "StreamUtilities.h"

using namespace std;
using namespace OpenFlight;

namespace
{
    string extractFilename(const std::string& iFilenamePath)
    {
        string r;
        size_t p = iFilenamePath.find_last_of("/\\");
        r = iFilenamePath.substr(p+1, string::npos);
        return r;
    }
    
    string extractPath(const std::string& iFilenamePath)
    {
        string r;
        size_t p = iFilenamePath.find_last_of("/\\");
        r = iFilenamePath.substr(0, p+1);
        return r;
    }
    
    // returns true, if the path is absolute.
    // 1- starting with a letter followed by a colon on Windows (d:\...)
    // 2- starting with a '/' on unix
    bool isPathAbsolute( const string& iPath )
    {
        regex e("^[a-zA-z]+:|^[\\/]+");
        smatch sm;
        regex_search(iPath, sm, e);
        return sm.size() > 0;
    }
    
    string makePathRelativeTo(const string iPathToRelativize, const string iReferencePath)
    {
        string r = iPathToRelativize;
        
        if( isPathAbsolute(r) )
        {
            // not implemented yet...
        }
        
        return r;
    }
}

//-----------------------------------------------------------------------------
OpenFlightReader::OpenFlightReader() :
mErrors(),
mHasDebugEnabled(false),
mpRootNode(nullptr),
mReadState(),
mReadStateStack()
{}

//-----------------------------------------------------------------------------
OpenFlightReader::~OpenFlightReader()
{}

//-----------------------------------------------------------------------------
void OpenFlightReader::addError(const std::string& iE) const
{
    mErrors += hasErrors() ? "\n" + iE : iE;
}

//-----------------------------------------------------------------------------
void OpenFlightReader::addPrimaryRecord(PrimaryRecord* iPr)
{
    if(mReadState.mpCurrentPrimaryNode != nullptr)
    {
        mReadState.mpCurrentPrimaryNode->addChild(iPr);
    }
    
    //push level always precede the addition of a Primary Record.
    //When it is activated, that primary record becomes the current
    //primary record.
    //
    if( mReadState.mIsPushLevelActivated )
    {
      mReadState.mpCurrentPrimaryNode = iPr;
      mReadState.mIsPushLevelActivated = false;
    }
}

//-----------------------------------------------------------------------------
void OpenFlightReader::addWarning(const std::string& iW) const
{
    mWarnings += hasWarnings() ? "\n" + iW : iW;
}

//-----------------------------------------------------------------------------
void OpenFlightReader::clear()
{
    mErrors = string();
    mWarnings = string();
    
    mpRootNode = nullptr;
    mReadState = ReadState();
    
    //clear the stack
    while ( !mReadStateStack.empty() )
    { mReadStateStack.pop(); }
}

//-----------------------------------------------------------------------------
void OpenFlightReader::enableDebug(bool iE)
{ mHasDebugEnabled = iE; }

//-----------------------------------------------------------------------------
bool OpenFlightReader::hasDebugEnabled() const
{ return mHasDebugEnabled; }

//-----------------------------------------------------------------------------
std::string OpenFlightReader::getAndClearLastErrors() const
{
    string r = mErrors;
    mErrors.clear();
    return r;
}

//-----------------------------------------------------------------------------
std::string OpenFlightReader::getAndClearLastWarnings() const
{
    string r = mWarnings;
    mWarnings.clear();
    return r;
}

//-----------------------------------------------------------------------------
HeaderRecord* OpenFlightReader::getCurrentHeaderNode()
{ return mReadState.mpCurrentHeaderNode; }

//-----------------------------------------------------------------------------
PrimaryRecord* OpenFlightReader::getCurrentPrimaryNode()
{ return mReadState.mpCurrentPrimaryNode; }

//-----------------------------------------------------------------------------
string OpenFlightReader::getFilePath() const
{ return mReadState.mFilePath; }

//-----------------------------------------------------------------------------
string OpenFlightReader::getFilename() const
{ return mReadState.mFilename; }

//-----------------------------------------------------------------------------
string OpenFlightReader::getFilenamePath() const
{ return mReadState.mFilenamePath; }

//-----------------------------------------------------------------------------
bool OpenFlightReader::hasErrors() const
{ return !mErrors.empty(); }

//-----------------------------------------------------------------------------
bool OpenFlightReader::hasWarnings() const
{ return !mWarnings.empty(); }

//-----------------------------------------------------------------------------
Document* OpenFlightReader::open(const std::string& iFileNamePath)
{
    open(iFileNamePath, false);
    Document *r = new Document();
    r->setRootNode(mpRootNode);
    return r;
}

//-----------------------------------------------------------------------------
void OpenFlightReader::open(const std::string& iFileNamePath,
                                    bool iIsExternalReference )
{
    // When it is not an external reference, we clear
    // all leftover from a previous open call.
    //
    // In the case of an external reference, we want to maintain
    // the internal state since the external reference will be
    // added to the current tree.
    if(!iIsExternalReference)
    {
        clear();
    }
    
    //first lets separate the filename from the path.
    mReadState.mFilenamePath = iFileNamePath;
    mReadState.mFilename = extractFilename(iFileNamePath);
    mReadState.mFilePath = extractPath(iFileNamePath);
    
    // Parse the file
    ifstream ifs;
    ifs.open(iFileNamePath, ifstream::in | ios_base::binary);

    if (!ifs.fail())
    {
        while (ifs.good() && !ifs.eof() && !hasErrors())
        { readRecord(ifs); }
    }
    else
    {
        ostringstream oss;
        oss << "OpenFlightReader::open - Could not open file " << iFileNamePath;
        addError( oss.str() );
    }
}

//-----------------------------------------------------------------------------
void OpenFlightReader::parseExternalReferenceRecord(const std::string& iRawRecord)
{
    ExternalReferenceRecord *extRef = new ExternalReferenceRecord(getCurrentPrimaryNode());
    ((Record*)extRef)->parseRecord(iRawRecord, getCurrentHeaderNode()->getFormatRevision());
    
    // Here, we push the read state because we are about
    // to call OpenFlightReader::open() again. A call to open
    // will break the internal reading state of the object. By
    // pushing right before and poping right after, we are
    // able to keep our internal reading state.
    //
    pushReadState();
    
    // force a pushLevel, so the external reference becomes the
    // parent of the whole hiearchy read in (namely a HeaderNode).
    //
    pushLevel();
    
    //ExtRef is a primary node, so we add it to the currentPrimaryNode
    addPrimaryRecord(extRef);
    
    // External reference filenamePath should always be relative.
    // In order to locate the file on disk, we prepend the relative
    // path with the current filePath.
    //
    // Sometimes, the external ref filename path is aboslute, we will
    // try to make it relative to the current filePath...
    string filenamePath = extRef->getFilenamePath();
    if( isPathAbsolute( filenamePath ) )
    {
        ostringstream oss;
        oss << "An absolute path was found while parsing an external reference: \n" <<
            filenamePath;
        addWarning(oss.str());
        
        filenamePath = makePathRelativeTo( filenamePath, getFilePath() );
    }
    
    // opening a specific node from an external ref is not
    // yet supported...
    //
    if( extRef->getNodeName().empty() )
    { open( getFilePath() + filenamePath, true ); }
    else
    {
        ostringstream oss;
        oss << "Loading a specific node from an external reference is not " <<
            "supported. This was encountered in file: \n" <<
            "\t" << getFilenamePath() << "\n With external reference: \n" <<
            "\t" << extRef->getFilenamePath() << " node: " << extRef->getNodeName() << endl;
        addError( oss.str() );
    }
    
    popReadState();
    
    // Maybe we should apply the palettes, accordingly with the flags
    // from the external ref, to the HeaderRecord that was just added.
}

//-----------------------------------------------------------------------------
void OpenFlightReader::parseHeaderRecord(const string& iRawRecord)
{
    HeaderRecord *r = new HeaderRecord(getCurrentPrimaryNode());
    ((Record*)r)->parseRecord( iRawRecord, 0 );
    
    addPrimaryRecord(r);
    
    // If there is no root node, it means the header record
    // encountered here is the first node of all document.
    // We need to initialize the root node and the current
    // primary node.
    //
    if (mpRootNode == nullptr)
    {
        mpRootNode = r;
    }
    
    // The header node always become the current primary node.
    // Since everything is attached to it
    //
    setCurrentPrimaryNode(r);
    // this should be revisited... It appears it is only needed
    // to access the current format revision while parsing...
    // If it is the case, it should be replaced by the
    // format revision...
    //
    setCurrentHeaderNode(r);
}

//-----------------------------------------------------------------------------
void OpenFlightReader::parseMissingRecord(const string& iRawRecord)
{
    Record r(nullptr);
    r.parseRecord(iRawRecord, 0);
    
    ostringstream oss;
    oss << "Record type '" << toString(r.getOpCode()) << "' was found but is not yet supported.";
    
    addWarning( oss.str() );
}

//-----------------------------------------------------------------------------
void OpenFlightReader::parseRawRecord(uint16_t iOpCode, const string& iRawRecord)
{
    switch (iOpCode)
    {
        case ocHeader: parseHeaderRecord(iRawRecord); break;
        case ocGroup: parsePrimaryRecord<GroupRecord>(iRawRecord); break;
        case ocObject: parsePrimaryRecord<ObjectRecord>(iRawRecord); break;
        case ocFace: parsePrimaryRecord<FaceRecord>(iRawRecord); break;
        case ocPushLevel: pushLevel(); break;
        case ocPopLevel: popLevel(); break;
        case ocColorPalette: parseAncillaryRecord<ColorPaletteRecord>(iRawRecord); break;
        case ocMatrix: parseAncillaryRecord<MatrixRecord>(iRawRecord); break;
        case ocExternalReference: parseExternalReferenceRecord(iRawRecord); break;
        case ocTexturePalette: parseAncillaryRecord<TexturePaletteRecord>(iRawRecord); break;
        case ocMaterialPalette: parseAncillaryRecord<MaterialPaletteRecord>(iRawRecord); break;
        case ocVertexPalette: parseAncillaryRecord<VertexPaletteRecord>(iRawRecord); break;
        case ocVertexWithColor:
        case ocVertexWithColorAndNormal:
        case ocVertexWithColorNormalAndUv:
        case ocVertexWithColorAndUv:
            parseVertexPaletteEntry(iRawRecord);
            break;
        case ocLightSourcePalette: parseAncillaryRecord<LightSourcePaletteRecord>(iRawRecord); break;
        case ocVertexList: parsePrimaryRecord<VertexListRecord>(iRawRecord); break;
        case ocTranslate: parseAncillaryRecord<TranslateRecord>(iRawRecord); break;
    default: parseMissingRecord(iRawRecord); break;
    }   
}

//-----------------------------------------------------------------------------
void OpenFlightReader::parseVertexPaletteEntry(const std::string& iRawRecord)
{
    // these are added to the vertex palette of the current header node
    // As stated by the specification, Vertex in the Vertex palette
    // entry will be referenced by Vertex List, Morph List ...
    //
    
    VertexPaletteRecord *r = mReadState.mpCurrentHeaderNode->getVertexPalette();
    if(r)
    {
        r->addVertexRawRecord(iRawRecord);
    }
    else{ addError("Vertex palette entry was found prior defining a Vertex Palette..."); }
}

//-----------------------------------------------------------------------------
void OpenFlightReader::popLevel()
{
    if( getCurrentPrimaryNode() && getCurrentPrimaryNode()->getParent() != nullptr )
    {
        //By construction, the parent of a primary record is a primary record.
        //see method addPrimaryRecord. Given that, we simply cast the parent
        //of the current primary record in a PrimaryRecord*.
        //
        setCurrentPrimaryNode( (PrimaryRecord*)getCurrentPrimaryNode()->getParent() );
    }
}

//-----------------------------------------------------------------------------
void OpenFlightReader::popReadState()
{
    mReadState = mReadStateStack.top();
    mReadStateStack.pop();
}

//-----------------------------------------------------------------------------
void OpenFlightReader::pushLevel()
{ mReadState.mIsPushLevelActivated = true; }

//-----------------------------------------------------------------------------
void OpenFlightReader::pushReadState()
{ mReadStateStack.push(mReadState); }

//-----------------------------------------------------------------------------
void OpenFlightReader::readRecord(ifstream& iFileStream)
{
    // read first 4 bytes of records, this will give us the opCode
    // and the length of the record to read.

    // record format
    //
    // |  2 bytes |  2 bytes       |  lengthOfRecord - 4 |
    // |  opCode  | LengthOfRecord |  recordPayload      | 
    //

    bool ok = true;
    uint16_t opCode = 0;
    uint16_t lengthOfRecord = 0;
    ok &= readUint16(iFileStream, opCode);
    ok &= readUint16(iFileStream, lengthOfRecord);
    
    if (ok)
    {
        char *rawRecord = new char[lengthOfRecord];

        // lets move back at the begining of the record and read the whole  thing
        //
        std::streamoff currentPosition = iFileStream.tellg();
        iFileStream.seekg( currentPosition - 4 );
        iFileStream.read(rawRecord, lengthOfRecord);

        if( iFileStream.good() )
        {
            string rawRecordAsString(rawRecord, lengthOfRecord);
            
            if( hasDebugEnabled() )
            {
                ostringstream oss;
                oss << toString((OpenFlight::opCode)opCode) << endl;
                oss << rawRecordToString(rawRecordAsString) << endl << endl;
                cout << oss.str();
            }

            parseRawRecord( opCode, rawRecordAsString );
        }
        else
        {
            ostringstream oss;
            oss << "OpenFlightReader::readRecord - Could not read record with opCode " << opCode <<
                " . " << lengthOfRecord << " bytes were requested, only " << iFileStream.gcount() << " could be read." ;
            addError( oss.str() );
        }

        delete[] rawRecord;
    }
    
    // when reaching eof, ok will be false (see istream documentation about eof, mostly due because
    // eof is set when trying to read pass the end...)
    //
    if( !ok && !iFileStream.eof() )
    { 
        ostringstream oss;
        oss << "OpenFlightReader::readRecord - Could not read record header with opCode " << opCode;
        addError( oss.str() );
    }
}

//-----------------------------------------------------------------------------
// iOpCode is in decimal
// iPayload is in hexadecimal at this point and not yet parsed.
//
string OpenFlightReader::rawRecordToString(const std::string& iRawRecord) const
{
    ostringstream oss;
    
    string hexPayload;
    char hex[5];
    for(size_t i = 0; i < iRawRecord.size(); ++i)
    { 
        sprintf(hex, "[%02X]", (unsigned char)iRawRecord[i]);
        hexPayload += hex;
    }

    oss << "Raw Record (hex):" << hexPayload;

    return oss.str();
}

//-----------------------------------------------------------------------------
void OpenFlightReader::setCurrentHeaderNode(HeaderRecord* ipR)
{ mReadState.mpCurrentHeaderNode = ipR; }

//-----------------------------------------------------------------------------
void OpenFlightReader::setCurrentPrimaryNode(PrimaryRecord* ipR)
{ mReadState.mpCurrentPrimaryNode = ipR; }