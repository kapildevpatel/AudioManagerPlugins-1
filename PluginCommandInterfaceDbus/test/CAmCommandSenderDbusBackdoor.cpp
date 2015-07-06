/**
 *  Copyright (c) 2012 BMW
 *
 *  \author Christian Linke, christian.linke@bmw.de BMW 2011,2012
 *
 *  \copyright
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction,
 *  including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 *  THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *  For further information see http://www.genivi.org/.
 */

#include "CAmCommandSenderDbusBackdoor.h"
#include "IAmCommand.h"


using namespace am;

CAmCommandSenderDbusBackdoor::CAmCommandSenderDbusBackdoor()
{
}

CAmCommandSenderDbusBackdoor::~CAmCommandSenderDbusBackdoor()
{
}

void CAmCommandSenderDbusBackdoor::setReceiveInterface(CAmCommandSenderDbus *sender, IAmCommandReceive* interface)
{
	sender->mpIAmCommandReceive=interface;
}

void CAmCommandSenderDbusBackdoor::setDbusConnection(CAmCommandSenderDbus *sender, DBusConnection *conn)
{
	sender->mCAmDbusMessageHandler.setDBusConnection(conn);
}

void CAmCommandSenderDbusBackdoor::setListSinks(CAmCommandSenderDbus *sender, std::vector<am_SinkType_s> newList)
{
	sender->mlistSinks=newList;
}

void CAmCommandSenderDbusBackdoor::setListSources(CAmCommandSenderDbus *sender, std::vector<am_SourceType_s> newList)
{
	sender->mlistSources=newList;
}












