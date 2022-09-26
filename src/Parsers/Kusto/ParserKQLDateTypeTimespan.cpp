#include <Parsers/IParserBase.h>
#include <Parsers/ExpressionListParsers.h>
#include <Parsers/Kusto/ParserKQLQuery.h>
#include <Parsers/Kusto/ParserKQLDateTypeTimespan.h>
#include <Common/StringUtils/StringUtils.h>
#include <cstdlib>
#include <unordered_map>
#include <format>
#include <math.h> 

namespace DB
{

bool ParserKQLDateTypeTimespan :: parseImpl(Pos & pos,  [[maybe_unused]] ASTPtr & node, Expected & expected)
{
    String token;
    const char * current_word = pos->begin;
    expected.add(pos, current_word);

    if (pos->type == TokenType::QuotedIdentifier || pos->type == TokenType::StringLiteral )
        token = String(pos->begin + 1, pos->end -1);
    else
        token = String(pos->begin, pos->end);
    if (!parseConstKQLTimespan(token))
        return false;

    return true;
}

double ParserKQLDateTypeTimespan :: toSeconds()
{
    switch (time_span_unit) 
    {
        case KQLTimespanUint::day:
            return time_span * 24 * 60 * 60;
        case KQLTimespanUint::hour:
            return time_span * 60 * 60;
        case KQLTimespanUint::minute:
            return time_span *  60;
        case KQLTimespanUint::second:
            return time_span ;
        case KQLTimespanUint::millisec:
            return time_span / 1000.0;
        case KQLTimespanUint::microsec:
            return time_span / 1000000.0;
        case KQLTimespanUint::nanosec:
            return time_span / 1000000000.0;
        case KQLTimespanUint::tick:
            return time_span / 10000000.0;
    }
}

bool ParserKQLDateTypeTimespan :: parseConstKQLTimespan(const String & text)
{
    std::unordered_map <String, KQLTimespanUint> TimespanSuffixes =
    {
        {"d", KQLTimespanUint::day},
        {"day", KQLTimespanUint::day},
        {"days", KQLTimespanUint::day},
        {"h", KQLTimespanUint::hour},
        {"hr", KQLTimespanUint::hour},
        {"hrs", KQLTimespanUint::hour},
        {"hour", KQLTimespanUint::hour},
        {"hours", KQLTimespanUint::hour},
        {"m", KQLTimespanUint::minute},
        {"min", KQLTimespanUint::minute},
        {"minute", KQLTimespanUint::minute},
        {"minutes", KQLTimespanUint::minute},
        {"s", KQLTimespanUint::second},
        {"sec", KQLTimespanUint::second},
        {"second", KQLTimespanUint::second},
        {"seconds", KQLTimespanUint::second},
        {"ms", KQLTimespanUint::millisec},
        {"milli", KQLTimespanUint::millisec},
        {"millis", KQLTimespanUint::millisec},
        {"millisec", KQLTimespanUint::millisec},
        {"millisecond", KQLTimespanUint::millisec},
        {"milliseconds", KQLTimespanUint::millisec},
        {"micro", KQLTimespanUint::microsec},
        {"micros", KQLTimespanUint::microsec},
        {"microsec", KQLTimespanUint::microsec},
        {"microsecond", KQLTimespanUint::microsec},
        {"microseconds", KQLTimespanUint::microsec},
        {"nano", KQLTimespanUint::nanosec},
        {"nanos", KQLTimespanUint::nanosec},
        {"nanosec", KQLTimespanUint::nanosec},
        {"nanosecond", KQLTimespanUint::nanosec},
        {"nanoseconds", KQLTimespanUint::nanosec},
        {"tick", KQLTimespanUint::tick},
        {"ticks", KQLTimespanUint::tick}
    };

    uint16_t days = 0, hours = 0, minutes = 0, seconds = 0 , sec_scale_len = 0;
    double nanoseconds = 00.00;

    const char * ptr = text.c_str();
    bool sign = false;

    auto scanDigit = [&](const char *start)
    {
        auto index = start;
        while (isdigit(*index))
            ++index;
        return index > start ? index - start : -1;
    };
    if (*ptr == '-')
    {
        sign = true;
        ++ptr;
    }
    int number_len = scanDigit(ptr);
    if (number_len <= 0)
      return false;

    days = std::stoi(String(ptr, ptr + number_len));

    if (*(ptr + number_len) == '.')
    {
        auto fractionLen = scanDigit(ptr + number_len + 1);
        if (fractionLen >= 0)
        {
            hours = std::stoi(String(ptr + number_len + 1, ptr + number_len + 1 + fractionLen));
            number_len += fractionLen + 1;
        }
    }
    else
    {
        hours = days;
        days = 0;
    }
    
    if (*(ptr + number_len) != ':')
    {
        String timespan_suffix(ptr + number_len, ptr + text.size());

        trim(timespan_suffix);
        if (TimespanSuffixes.find(timespan_suffix) == TimespanSuffixes.end())
            return false;

        time_span = std::stod(String(ptr, ptr + number_len));
        time_span_unit = TimespanSuffixes[timespan_suffix] ;

        return true;
    }
    
    if (hours > 23)
        return false;
    
    auto min_len = scanDigit(ptr + number_len + 1);
    if (min_len < 0)
        return false;

    minutes = std::stoi(String(ptr + number_len + 1, ptr + number_len + 1 + min_len));
    if (minutes > 59)
        return false;

    number_len += min_len + 1;
    if (*(ptr + number_len) == ':')
    {
        auto sec_len = scanDigit(ptr + number_len + 1);
        if (sec_len > 0)
        {
            seconds = std::stoi(String(ptr + number_len + 1, ptr + number_len + 1 + sec_len));
            if (seconds > 59)
                return false;

            number_len += sec_len + 1;
            if (*(ptr + number_len) == '.')
            {
                sec_scale_len = scanDigit(ptr + number_len + 1);
                if (sec_scale_len > 0)
                {
                    nanoseconds = std::stoi(String(ptr + number_len + 1, ptr + number_len + 1 + sec_scale_len));

                    if (nanoseconds > 1000000000)
                        return false;
                }
            }
        }
    }
    auto exponent = 9 - sec_scale_len; // max supported length of fraction of seconds is 9 
    nanoseconds = nanoseconds * pow(10, exponent );

    if (sign)
        time_span = -(days * 86400 + hours * 3600 + minutes * 60 + seconds + (nanoseconds /1000000000 ));
    else
        time_span = days * 86400 + hours * 3600 + minutes * 60 + seconds + (nanoseconds /1000000000 );
    
    time_span_unit = KQLTimespanUint::second;

    return true;
}

}
