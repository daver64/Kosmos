#undef NDEBUG
#include "weather.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace kosmos;

int main()
{
    Metar m;
    assert(decode_metar("METAR EFHK 271750Z 24012G22KT 9999 -RA SCT025 BKN040 08/05 Q1008", m));
    assert(m.station == "EFHK" && m.day == 27 && m.hour == 17 && m.minute == 50);
    assert(m.wind_dir == 240 && m.wind_kt == 12 && m.gust_kt == 22);
    assert(m.visibility_m == 9999 && m.weather.size() == 1 && m.weather[0] == "-RA");
    assert(m.clouds.size() == 2 && m.clouds[1].cover == "BKN" && m.clouds[1].base_ft == 4000);
    assert(m.temperature_c == 8 && m.dewpoint_c == 5 && m.qnh_hpa == 1008);

    // US style: statute miles, inches of mercury, remarks ignored, negative temperatures.
    assert(decode_metar("KJFK 271751Z 31015KT 1 1/2SM +TSRA BR OVC008 M02/M04 A2992 RMK AO2 SLP132", m));
    assert(m.wind_dir == 310 && m.visibility_m > 2300 && m.visibility_m < 2500);
    assert(m.weather.size() == 2 && m.weather[0] == "+TSRA" && m.temperature_c == -2 && m.dewpoint_c == -4 && m.qnh_hpa == 1013);
    assert(m.clouds.size() == 1 && m.clouds[0].cover == "OVC");

    assert(decode_metar("LFPG 271730Z VRB02KT CAVOK 12/03 Q1021 NOSIG", m));
    assert(m.wind_dir == -1 && m.clouds.empty() && m.visibility_m == 9999);
    assert(decode_metar("ENGM 271750Z 10005MPS 4000 FG VV002 01/01 Q0998", m));
    assert(m.wind_kt == 10 && m.visibility_m == 4000 && m.clouds.size() == 1 && m.clouds[0].base_ft == 200);
    assert(!decode_metar("hello world", m));
    assert(!decode_metar("EFHK", m));

    // Encoding and decoding give the same observation back.
    Metar a;
    a.station = "XMAA";
    a.day = 3;
    a.hour = 4;
    a.minute = 10;
    a.wind_dir = 5;
    a.wind_kt = 8;
    a.gust_kt = 19;
    a.visibility_m = 1200;
    a.weather = {"+RA", "BR"};
    a.clouds = {{"FEW", 1200}, {"OVC", 3000}};
    a.temperature_c = -7;
    a.dewpoint_c = -9;
    a.qnh_hpa = 1003;
    a.valid = true;
    const std::string line = encode_metar(a);
    assert(line == "XMAA 030410Z 00508G19KT 1200 +RA BR FEW012 OVC030 M07/M09 Q1003");
    Metar b;
    assert(decode_metar(line, b));
    assert(encode_metar(b) == line);

    // Conditions
    const Conditions c = conditions_from(b);
    assert(c.rain == 1.0 && c.visibility == 1200.0 && c.cloud_cover > 0.9);
    assert(std::fabs(c.cloud_base - 3000 * 0.3048) < 1.0);
    assert(c.wind_north < 0.0 && std::fabs(c.gust - 11 * 0.514444) < 0.01);

    // Dates
    int d, h, mi;
    unix_to_dhm(1761587400.0, d, h, mi); // 2025-10-27 17:50 UTC
    assert(d == 27 && h == 17 && mi == 50);
    assert(std::fabs(metar_time(27, 17, 50, 1761587400.0 + 3600.0) - 1761587400.0) < 1.0);
    assert(std::fabs(metar_time(31, 23, 50, 1761957000.0) - 1761954600.0) < 1.0); // across a month boundary


    // A procedural field: 64 stations, valid METARs, smooth sampling, deterministic.
    BodyWeather earth = body_weather_for("Earth", 1.225, 288.0, KOSMOS_ROOT);
    WeatherField field("Earth", earth, 5);
    assert(field.stations().size() == 64);
    field.update(1761587400.0);
    int raining = 0;
    for (const WeatherStation &s : field.stations())
    {
        Metar t;
        assert(decode_metar(s.line, t) && t.station == s.id);
        raining += s.now.rain > 0.0;
    }
    std::printf("earth: %d of 64 stations raining\n", raining);
    assert(raining > 2 && raining < 40);
    std::printf("%s\n%s\n", field.stations()[0].line.c_str(), field.stations()[17].line.c_str());
    const Conditions here = field.sample(60.17, 24.94);
    const Conditions near = field.sample(60.18, 24.95);
    assert(std::fabs(here.wind_east - near.wind_east) < 0.5 && std::fabs(here.pressure - near.pressure) < 2.0);
    WeatherField again("Earth", earth, 5);
    again.update(1761587400.0);
    assert(again.stations()[5].line == field.stations()[5].line);
    // Time moves the weather on, and the ten-minute blend is continuous.
    const Conditions t0 = field.sample(10.0, 10.0);
    field.update(1761587400.0 + 590.0);
    const Conditions t1 = field.sample(10.0, 10.0);
    field.update(1761587400.0 + 610.0);
    const Conditions t2 = field.sample(10.0, 10.0);
    assert(std::fabs(t1.pressure - t2.pressure) < 1.5 && std::fabs(t1.wind_east - t2.wind_east) < 3.0);
    (void)t0;

    // A live observation replaces a station, and clearing it brings the procedural one back.
    const std::string id = field.stations()[3].id;
    assert(field.set_live(id + " 271750Z 27030G45KT 2000 +RA OVC010 10/09 Q0990", 0.0));
    assert(field.stations()[3].live && field.stations()[3].now.rain == 1.0);
    assert(!field.set_live("QQQQ 271750Z 27030KT 9999 NSC 10/09 Q1000", 0.0));
    field.clear_live();
    field.update(1761587400.0 + 620.0);
    assert(!field.stations()[3].live);

    // Dry worlds do not rain; Mars gets dust, Venus is hot.
    WeatherField mars("Mars", body_weather_for("Mars", 0.02, 210.0, KOSMOS_ROOT), 9);
    mars.update(1761587400.0);
    for (const WeatherStation &s : mars.stations())
        assert(s.now.rain == 0.0);
    WeatherField venus("Venus", body_weather_for("Venus", 65.0, 735.0, KOSMOS_ROOT), 9);
    venus.update(1761587400.0);
    assert(venus.stations()[0].now.temperature > 90.0);
    assert(WeatherField::station_id("Mars", 0) == "MSAA" && WeatherField::station_id("Mars", 27) == "MSBB");
    std::printf("weather_test ok\n");
    return 0;
}
