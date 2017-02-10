///
/// \package astlib
/// \file ast2json.cpp
///
/// \author Marian Krivos <marian.krivos@rsys.sk>
/// \date 9Feb.,2017 
/// \brief definicia typu
///
/// (C) Copyright 2017 R-SYS s.r.o
/// All rights reserved.
///

#include "astlib/BinaryAsterixDekoder.h"
#include "astlib/CodecRegister.h"
#include "astlib/Exception.h"

#include <Poco/NumberParser.h>
#include "Poco/Util/Application.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/Util/AbstractConfiguration.h"
#include "Poco/AutoPtr.h"

#include "Poco/Net/Net.h"
#include "Poco/Net/DatagramSocket.h"
#include "Poco/Net/SocketAddress.h"

#include "Poco/JSON/JSON.h"
#include "Poco/JSON/Object.h"
#include "Poco/JSON/Query.h"
#include "Poco/JSON/JSONException.h"

#include <deque>
#include <iostream>
#include <sstream>

using Poco::Util::Application;
using Poco::Util::Option;
using Poco::Util::OptionSet;
using Poco::Util::HelpFormatter;
using Poco::Util::AbstractConfiguration;
using Poco::Util::OptionCallback;
using Poco::AutoPtr;
using Poco::Net::Socket;
using Poco::Net::DatagramSocket;
using Poco::Net::SocketAddress;
using Poco::Net::IPAddress;


class SampleApp: public Application
{
public:
    SampleApp() :
        _helpRequested(false)
    {
    }

    class MyValueDecoder :
        public astlib::TypedValueDecoder
    {
        Poco::JSON::Object::Ptr json;
        std::deque<Poco::JSON::Object::Ptr> scopes;
        Poco::JSON::Array::Ptr localArray;

        void setScope(Poco::JSON::Object::Ptr obj)
        {
            if (scopes.empty())
                scopes.push_back(obj);
            else
                scopes.back() = obj;
        }

        void addScope(Poco::JSON::Object::Ptr obj)
        {
            scopes.push_back(obj);
        }

        void removeScope()
        {
            auto array = *scopes.back();
            scopes.pop_back();
        }

        Poco::JSON::Object::Ptr scope()
        {
            poco_assert(!scopes.empty());
            return scopes.back();
        }
        // ----------------------------------------------------------

        virtual void begin()
        {
            json = new Poco::JSON::Object();
            //json->set("message", "ast");
        }
        virtual void dataItem(const astlib::ItemDescription& uapItem)
        {
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            json->set(uapItem.getDescription(), item);
            setScope(item);
        }
        virtual void beginRepetitive(int count)
        {
            Poco::JSON::Array::Ptr array = localArray = new Poco::JSON::Array();
            scope()->set("array", array);

            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            addScope(item);
            //array->add(item);
        }
        virtual void repetitiveItem(int index)
        {
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            setScope(item);
            localArray->add(item);
        }
        virtual void endRepetitive()
        {
            removeScope();
        }
#if 0
        virtual void decode(Poco::UInt64 value, const astlib::ValueDecoder::Context& ctx)
        {
        }
#endif
        virtual void decodeBoolean(const std::string& identification, bool value)
        {
            scope()->set(identification, Poco::Dynamic::Var(value));
        }
        virtual void decodeSigned(const std::string& identification, Poco::Int64 value)
        {
            scope()->set(identification, Poco::Dynamic::Var(value));
        }
        virtual void decodeUnsigned(const std::string& identification, Poco::UInt64 value)
        {
            scope()->set(identification, Poco::Dynamic::Var(value));
        }
        virtual void decodeReal(const std::string& identification, double value)
        {
            scope()->set(identification, Poco::Dynamic::Var(value));
        }
        virtual void decodeString(const std::string& identification, const std::string& value)
        {
            scope()->set(identification, Poco::Dynamic::Var(value));
        }

        virtual void end()
        {
            json->stringify(std::cout, 2);
            std::cout << std::endl;
            json = nullptr;
            removeScope();
        }
    } decoderHandler;

protected:
    void initialize(Application& self)
    {
        loadConfiguration(); // load default configuration files, if present
        Application::initialize(self);
        // add your own initialization code here
    }

    void uninitialize()
    {
        // add your own uninitialization code here
        Application::uninitialize();
    }

    void reinitialize(Application& self)
    {
        Application::reinitialize(self);
        // add your own reinitialization code here
    }

    void defineOptions(OptionSet& options)
    {
        Application::defineOptions(options);

        options.addOption(Option("help", "h", "display help information on command line arguments").required(false).repeatable(false).callback(OptionCallback<SampleApp>(this, &SampleApp::handleHelp)));
        options.addOption(Option("config", "c", "load configuration data from a file").required(false).repeatable(false).argument("file").callback(OptionCallback<SampleApp>(this, &SampleApp::handleConfig)));
        options.addOption(Option("port", "p", "bind to UDP port").required(true).repeatable(false).argument("value").callback(OptionCallback<SampleApp>(this, &SampleApp::handlePort)));
    }

    void handleHelp(const std::string& name, const std::string& value)
    {
        _helpRequested = true;
        displayHelp();
        stopOptionsProcessing();
    }

    void handleConfig(const std::string& name, const std::string& value)
    {
        loadConfiguration(value);
    }

    void handlePort(const std::string& name, const std::string& port)
    {
        _port = Poco::NumberParser::parse(port);
    }

    void displayHelp()
    {
        HelpFormatter helpFormatter(options());
        helpFormatter.setCommand(commandName());
        helpFormatter.setUsage("OPTIONS");
        helpFormatter.setHeader("Simple Asterix From Network To Json Decode & Display Application.");
        helpFormatter.format(std::cout);
    }

    void prepareDecoders()
    {
        astlib::CodecRegister codecRegister;
        codecRegister.populateCodecsFromDirectory("specs");
        auto codecs = codecRegister.enumerateCodecsByCategory();

        for(auto codec: codecs)
        {
            logger().information("registering %s", codec->getCategoryDescription().toString());
            _codecs[codec->getCategoryDescription().getCategory()] = codec;
        }
    }

    int main(const ArgVec& args)
    {
        if (!_helpRequested)
        {
            try
            {
                prepareDecoders();

                _socket.bind(SocketAddress(_port), true);
                logger().information("Listening on udp port %d", _port);

                Poco::Timespan span(250000);
                while (!_stop)
                {
                    if (_socket.poll(span, Socket::SELECT_READ))
                    {
                        try
                        {
                            astlib::Byte buffer[astlib::BinaryAsterixDekoder::MAX_PACKET_SIZE];
                            SocketAddress sender;
                            int bytes = _socket.receiveFrom(buffer, sizeof(buffer), sender);

                            if (bytes > 0)
                            {
                                //logger().information("received %d bytes", bytes);

                                int category = buffer[0];
                                auto codec = _codecs[category];
                                if (codec)
                                {
#if 0
                                    unsigned char bytes[33+1+16+2+2+8+3+6+2+4+4+2+4+3+2+4+2+2+7+1+2+1+2] = {
                                        48, // CAT
                                        0, 1+2+3+2+3+1+4+2+2+3+6+17+16+2+4+4+2+4+3+2+4+2+2+7+1+2+1+2, // size
                                        0xFF, 0xFF, 0xFF, 0xF8,// FSPEC

                                        5, 6,  // 10 - sac sic
                                        0, 0, 200, // 140 - time of day
                                        0xfe, // 20 - Target Report Descriptor
                                        0xFF, 0xFF,0xFF, 0xFF, // 40 - polar coords
                                        0xFF, 0xFF, // 70 - mode 3A
                                        0xFF, 0xFF, // 90 - mode C
                                        0xFE, 0x88, 0x44,0x88, 0x44, 0x88, 0x44, 0x88, // 130 - plot characteristics

                                        0xFF, 0xFF, 0xFF, // 220 - aircraft address
                                        0x42, 0x55, 0x12, 0x45, 0x42, 0x24, // 240 - Aircraft Identification
                                        2, 4,4,4,4,4,4,2,1,  5,5,5,5,5,5,2,1,  // item 250 Mode S Comm B data
                                        1,0, // 161 - track number
                                        0xFF, 0xFF, 0x00, 0x01, // 42 - cartes coords
                                        0xFF, 0xFF, 0xFF, 0xFF, // 200
                                        0xFF, 0xFE, // 170 - track status

                                        0xFF, 0xFF, 0xFF, 0xFF, // 210 - track quality
                                        3,5,6, // 30 - WE Condition
                                        0xFF, 0xFE, // 80 - mode 3a conf
                                        0xFF, 0xFF, 0xFF, 0xFF, // 100 - mode C conf
                                        0xFF, 0xFF, // 110 - 3d height
                                        0xC0, 0xFF,0xFF, 2, 0,1,0,2,0,3, 0,1,0,2,0,3, // 120 - radial dopler speed
                                        0xFF, 0xFF, // 230 - Acas comm

                                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 260 - ACAS resolution
                                        0xFF, // 55
                                        0xFF, 0xFF,// 50
                                        0xFF, // 65
                                        0xFF, 0xFF,// 60

                                    };
                                    _decoder.decode(*codec, decoderHandler, bytes, sizeof(bytes));
#else
                                    _decoder.decode(*codec, decoderHandler, buffer, bytes);
#endif
                                }
                            }
                        }
                        catch (Poco::Exception& exc)
                        {
                            std::cerr << "ast2json: " << exc.displayText() << std::endl;
                        }
                    }
                }
            }
            catch(astlib::Exception& e)
            {
                logger().error(e.displayText());
                return Application::EXIT_OK;
            }
        }
        return Application::EXIT_OK;
    }

private:
    std::map<int, astlib::CodecDescriptionPtr> _codecs;
    astlib::BinaryAsterixDekoder _decoder;
    Poco::Net::DatagramSocket _socket;
    int _port = 10000;
    bool _helpRequested;
    bool _stop = false;
};

POCO_APP_MAIN(SampleApp)

