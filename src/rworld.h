#ifndef RWORLD_WORLD_H
#define RWORLD_WORLD_H

#include <cstdint>
#include <memory>
#include <vector>

namespace rworld {

/**
 * Biome types based on temperature and moisture levels
 */
enum class BiomeType {
    // Cold biomes
    TUNDRA,
    TAIGA,
    
    // Temperate biomes
    GRASSLAND,
    TEMPERATE_DECIDUOUS_FOREST,
    TEMPERATE_RAINFOREST,
    
    // Warm/Hot biomes
    SAVANNA,
    TROPICAL_SEASONAL_FOREST,
    TROPICAL_RAINFOREST,
    
    // Dry biomes
    COLD_DESERT,
    DESERT,
    
    // Special biomes
    OCEAN,
    DEEP_OCEAN,
    BEACH,
    SNOW,
    ICE,
    
    // Mountain variants
    MOUNTAIN_TUNDRA,
    MOUNTAIN_FOREST,
    MOUNTAIN_PEAK
};

/**
 * Precipitation type based on temperature
 */
enum class PrecipitationType {
    NONE,
    RAIN,
    SNOW,
    SLEET
};

/**
 * Soil type based on particle size and composition
 */
enum class SoilType {
    CLAY,           // Fine particles, poor drainage, high nutrients
    SILT,           // Medium particles, good fertility
    SAND,           // Coarse particles, good drainage, low nutrients
    LOAM,           // Balanced mix, ideal for agriculture
    PEAT,           // Organic-rich, acidic, wetland soils
    ROCKY,          // Minimal soil, mountain/desert regions
    PERMAFROST,     // Frozen soil, arctic regions
    NONE            // Water/ice surfaces
};

/**
 * Data types available for batch queries
 */
enum class DataType {
    TERRAIN_HEIGHT,
    TEMPERATURE,
    TEMPERATURE_AT_TIME,
    BIOME,
    PRECIPITATION,
    CURRENT_PRECIPITATION,
    PRECIPITATION_TYPE,
    AIR_PRESSURE,
    HUMIDITY,
    WIND_SPEED,
    CURRENT_WIND_SPEED,
    WIND_DIRECTION,
    CURRENT_WIND_DIRECTION,
    IS_RIVER,
    RIVER_WIDTH,
    FLOW_ACCUMULATION,
    IS_VOLCANO,
    COAL_DEPOSIT,
    IRON_DEPOSIT,
    OIL_DEPOSIT,
    INSOLATION,
    IS_DAYLIGHT,
    SOLAR_ANGLE,
    VEGETATION_DENSITY,
    SOIL_TYPE,
    SOIL_FERTILITY,
    SOIL_PH,
    ORGANIC_MATTER,
    PRESSURE_AT_LOCATION,
    PRESSURE_GRADIENT,
    IS_STORM_FRONT
};

/**
 * Location for batch queries
 */
struct Location {
    float longitude;
    float latitude;
    float altitude;
    float current_time;  // Used for time-dependent queries
    float detail_level;  // Used for terrain queries
    
    Location(float lon = 0.0f, float lat = 0.0f, float alt = 0.0f, 
             float time = 12.0f, float detail = 1.0f)
        : longitude(lon), latitude(lat), altitude(alt), 
          current_time(time), detail_level(detail) {}
};

/**
 * Results from batch queries
 * Only requested data types will have populated vectors
 */
struct BatchResult {
    std::vector<float> terrain_height;
    std::vector<float> temperature;
    std::vector<float> precipitation;
    std::vector<float> air_pressure;
    std::vector<float> humidity;
    std::vector<float> wind_speed;
    std::vector<float> wind_direction;
    std::vector<float> river_width;
    std::vector<float> flow_accumulation;
    std::vector<float> coal_deposit;
    std::vector<float> iron_deposit;
    std::vector<float> oil_deposit;
    std::vector<float> insolation;
    std::vector<float> solar_angle;
    std::vector<float> vegetation_density;
    std::vector<float> soil_fertility;
    std::vector<float> soil_ph;
    std::vector<float> organic_matter;
    std::vector<float> pressure_at_location;
    std::vector<float> pressure_gradient;
    
    std::vector<BiomeType> biome;
    std::vector<PrecipitationType> precipitation_type;
    std::vector<SoilType> soil_type;
    
    std::vector<bool> is_river;
    std::vector<bool> is_volcano;
    std::vector<bool> is_daylight;
    std::vector<bool> is_storm_front;
    
    size_t count = 0;  // Number of locations queried
};

/**
 * Configuration for world generation
 */
struct WorldConfig {
    uint64_t seed = 12345;
    float world_scale = 1.0f;
    int day_of_year = 80; // Day of year (0-364), default is spring equinox (March 21)
    
    // Temperature parameters (Celsius)
    float equator_temperature = 30.0f;
    float pole_temperature = -40.0f;
    float temperature_lapse_rate = 6.5f; // °C per 1000m altitude
    
    // Terrain parameters
    float sea_level = 0.0f;
    float max_terrain_height = 8848.0f; // Mt. Everest height in meters
    
    // Noise parameters
    float terrain_frequency = 0.001f;
    int terrain_octaves = 6;
    float terrain_lacunarity = 2.0f;
    float terrain_gain = 0.5f;
    
    float moisture_frequency = 0.002f;
    int moisture_octaves = 4;
};

/**
 * World - A living, active world generator
 * 
 * Provides procedural generation of environmental parameters including
 * terrain, biomes, temperature, precipitation, and atmospheric conditions.
 * 
 * Coordinates:
 * - Longitude: -180 to 180 degrees (West to East)
 * - Latitude: -90 to 90 degrees (South to North)  
 * - Altitude: meters above sea level
 */
class World {
public:
    /**
     * Construct a world with default configuration
     */
    World();
    
    /**
     * Construct a world with custom configuration
     */
    explicit World(const WorldConfig& config);
    
    ~World();
    
    // Prevent copying (noise generators are not copyable)
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    
    // Allow moving
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    
    /**
     * Get the biome at a specific location
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters (use get_terrain_height if unknown)
     * @return BiomeType at the location
     */
    BiomeType get_biome(float longitude, float latitude, float altitude) const;
    
    /**
     * Get temperature at a specific location
     * 
     * Temperature is calculated based on:
     * - Latitude (warmer at equator, colder at poles)
     * - Altitude (decreases with height)
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Temperature in degrees Celsius
     */
    float get_temperature(float longitude, float latitude, float altitude) const;
    
    /**
     * Get temperature at a specific location and time
     * 
     * This includes dynamic effects from:
     * - Base temperature (latitude, altitude, season)
     * - Solar insolation (time of day)
     * - Cloud cover (cooling effect)
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @param current_time Time of day in hours (0-24)
     * @return Temperature in degrees Celsius including dynamic effects
     */
    float get_temperature_at_time(float longitude, float latitude, float altitude, float current_time) const;
    
    /**
     * Get terrain height at a location (ignoring altitude parameter)
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param detail_level Detail multiplier for terrain resolution (1.0 = normal, higher = finer detail)
     * @return Height in meters above sea level (negative for underwater)
     */
    float get_terrain_height(float longitude, float latitude, float detail_level = 1.0f) const;
    
    /**
     * Get precipitation level at a location
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Precipitation in mm per year
     */
    float get_precipitation(float longitude, float latitude, float altitude) const;
    
    /**
     * Get instantaneous precipitation at a location and time
     * 
     * Includes temporal weather variation. Use this for current rain/snow conditions.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @param current_time Time in hours (used for weather system movement)
     * @return Current precipitation rate (0-1, where 1 = heavy rain/snow)
     */
    float get_current_precipitation(float longitude, float latitude, float altitude, float current_time) const;
    
    /**
     * Get the type of precipitation at a location
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Type of precipitation
     */
    PrecipitationType get_precipitation_type(float longitude, float latitude, float altitude) const;
    
    /**
     * Get atmospheric pressure at a location
     * 
     * Pressure decreases exponentially with altitude
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Pressure in millibars (hPa)
     */
    float get_air_pressure(float longitude, float latitude, float altitude) const;
    
    /**
     * Get humidity at a location (relative humidity 0-1)
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Relative humidity (0.0 to 1.0)
     */
    float get_humidity(float longitude, float latitude, float altitude) const;
    
    /**
     * Get wind speed at a location
     * 
     * Wind is influenced by pressure gradients, latitude (Coriolis effect),
     * and terrain features
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Wind speed in meters per second
     */
    float get_wind_speed(float longitude, float latitude, float altitude) const;
    
    /**
     * Get wind speed at a location and time
     * 
     * Includes temporal weather variation for current wind conditions.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @param current_time Time in hours (used for weather system movement)
     * @return Current wind speed in meters per second
     */
    float get_current_wind_speed(float longitude, float latitude, float altitude, float current_time) const;
    
    /**
     * Get wind direction at a location
     * 
     * Direction in degrees where 0° is North, 90° is East, 180° is South, 270° is West
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Wind direction in degrees (0-360)
     */
    float get_wind_direction(float longitude, float latitude, float altitude) const;
    
    /**
     * Get wind direction at a location and time
     * 
     * Includes temporal weather variation for current wind conditions.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @param current_time Time in hours (used for weather system movement)
     * @return Current wind direction in degrees (0-360)
     */
    float get_current_wind_direction(float longitude, float latitude, float altitude, float current_time) const;
    
    /**
     * Check if there is a river at this location
     * 
     * Rivers flow from high elevation to low, following terrain gradients.
     * More common in areas with high precipitation.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @return true if location has a river
     */
    bool is_river(float longitude, float latitude) const;
    
    /**
     * Get river width at a location
     * 
     * Width increases with upstream drainage area and precipitation.
     * Returns 0 if no river present.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @return River width in meters (0 if no river)
     */
    float get_river_width(float longitude, float latitude) const;
    
    /**
     * Get flow accumulation at a location
     * 
     * Represents the upstream drainage area contributing water to this point.
     * Higher values indicate major river channels.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @return Flow accumulation value (0-1, normalized)
     */
    float get_flow_accumulation(float longitude, float latitude) const;
    
    /**
     * Check if there is a volcano at this location
     * 
     * Volcanoes are cone-shaped features scattered across the terrain,
     * more common at higher elevations.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @return true if location is part of a volcano
     */
    bool is_volcano(float longitude, float latitude) const;
    
    /**
     * Get coal deposit concentration at a location
     * 
     * Coal forms in ancient swamps and forested lowlands.
     * Found in sedimentary rocks at moderate depths.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @return Coal concentration (0-1, 0=none, 1=rich deposit)
     */
    float get_coal_deposit(float longitude, float latitude) const;
    
    /**
     * Get iron ore deposit concentration at a location
     * 
     * Iron ore forms in ancient seabeds and volcanic regions.
     * Often found in banded iron formations.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @return Iron ore concentration (0-1, 0=none, 1=rich deposit)
     */
    float get_iron_deposit(float longitude, float latitude) const;
    
    /**
     * Get oil deposit concentration at a location
     * 
     * Oil forms in ancient ocean sediments under specific conditions.
     * Typically found in sedimentary basins.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @return Oil concentration (0-1, 0=none, 1=rich deposit)
     */
    float get_oil_deposit(float longitude, float latitude) const;
    
    /**
     * Get insolation (solar radiation) at a location
     * 
     * Insolation depends on:
     * - Latitude (maximum at equator, minimum at poles)
     * - Time of day (solar angle based on longitude and current_time)
     * - Cloud cover (reduces insolation)
     * - Season (axial tilt, if implemented)
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param current_time Time of day in hours (0-24, where 12.0 is solar noon at 0° longitude)
     * @return Insolation in W/m² (0-1400, 0=night, ~1000=clear day at equator)
     */
    float get_insolation(float longitude, float latitude, float current_time) const;
    
    /**
     * Check if a location is in daylight
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param current_time Time of day in hours (0-24, where 12.0 is solar noon at 0° longitude)
     * @return true if the sun is above the horizon
     */
    bool is_daylight(float longitude, float latitude, float current_time) const;
    
    /**
     * Get the solar angle above horizon at a location
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param current_time Time of day in hours (0-24, where 12.0 is solar noon at 0° longitude)
     * @return Solar elevation angle in degrees (negative = below horizon)
     */
    float get_solar_angle(float longitude, float latitude, float current_time) const;
    
    /**
     * Get vegetation density at a location
     * 
     * Vegetation density is based on temperature, precipitation, and biome type.
     * Useful for determining tree cover, foliage rendering, or resource availability.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Vegetation density (0-1, where 0=barren, 1=dense forest/jungle)
     */
    float get_vegetation_density(float longitude, float latitude, float altitude) const;
    
    /**
     * Get soil type at a location
     * 
     * Soil type is determined by climate, terrain, biome, and drainage.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return SoilType at the location
     */
    SoilType get_soil_type(float longitude, float latitude, float altitude) const;
    
    /**
     * Get soil fertility at a location
     * 
     * Fertility depends on soil type, organic matter, precipitation, and temperature.
     * Higher values indicate better agricultural potential.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Fertility rating (0-1, where 0=barren, 1=highly fertile)
     */
    float get_soil_fertility(float longitude, float latitude, float altitude) const;
    
    /**
     * Get soil pH at a location
     * 
     * pH affects nutrient availability and what plants can grow.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return pH value (typical range 4.0-9.0, where 7.0=neutral)
     */
    float get_soil_ph(float longitude, float latitude, float altitude) const;
    
    /**
     * Get organic matter content in soil
     * 
     * Organic matter improves soil structure, water retention, and fertility.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @return Organic matter content (0-1, where 0=none, 1=peat/very high)
     */
    float get_organic_matter(float longitude, float latitude, float altitude) const;
    
    /**
     * Get atmospheric pressure at a location including weather systems
     * 
     * Pressure varies with altitude and weather patterns (highs/lows).
     * High pressure = clear weather, low pressure = storms.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param altitude Altitude in meters
     * @param current_time Time in hours (used for moving pressure systems)
     * @return Pressure in millibars/hPa (typical range 950-1050)
     */
    float get_pressure_at_location(float longitude, float latitude, float altitude, float current_time) const;
    
    /**
     * Get pressure gradient magnitude at a location
     * 
     * Large gradients indicate strong winds and potential storm activity.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param current_time Time in hours
     * @return Pressure gradient in mb per degree (typical range 0-5)
     */
    float get_pressure_gradient(float longitude, float latitude, float current_time) const;
    
    /**
     * Check if location is near a storm front
     * 
     * Fronts occur where pressure gradients are steep, bringing
     * rapid weather changes, precipitation, and strong winds.
     * 
     * @param longitude Longitude in degrees (-180 to 180)
     * @param latitude Latitude in degrees (-90 to 90)
     * @param current_time Time in hours
     * @return true if near a storm front (steep pressure gradient)
     */
    bool is_storm_front(float longitude, float latitude, float current_time) const;
    
    /**
     * Batch query multiple locations efficiently
     * 
     * This method is optimized for querying many locations at once, providing
     * 10-100x better performance than individual queries for bulk operations.
     * 
     * @param locations Vector of Location structs with coordinates and parameters
     * @param data_types Vector of DataType enum values specifying what data to retrieve
     * @return BatchResult containing only the requested data types
     * 
     * Example:
     * ```cpp
     * std::vector<Location> locs = {{0, 45, 0}, {10, 45, 100}, {20, 45, 500}};
     * std::vector<DataType> types = {DataType::TEMPERATURE, DataType::BIOME};
     * BatchResult result = world.batch_query(locs, types);
     * // result.temperature and result.biome are populated
     * ```
     */
    BatchResult batch_query(const std::vector<Location>& locations,
                           const std::vector<DataType>& data_types) const;
    
    /**
     * Update the world configuration
     * This will reset internal noise generators
     */
    void set_config(const WorldConfig& config);
    
    /**
     * Get the current configuration
     */
    const WorldConfig& get_config() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

/**
 * Convert BiomeType to string name
 */
const char* biome_to_string(BiomeType biome);

/**
 * Convert SoilType to string name
 */
const char* soil_to_string(SoilType soil);

} // namespace rworld


#ifdef _RWORLD_IMPLEMENTATION
#include "FastNoiseLite.h"
#include <cmath>
#include <algorithm>

namespace rworld {

// PIMPL implementation to hide FastNoiseLite from the header
class World::Impl {
public:
    WorldConfig config;
    FastNoiseLite terrain_noise;
    FastNoiseLite moisture_noise;
    FastNoiseLite temperature_variation_noise;
    FastNoiseLite wind_noise;
    FastNoiseLite river_noise;
    FastNoiseLite volcano_noise;
    FastNoiseLite coal_noise;
    FastNoiseLite iron_noise;
    FastNoiseLite oil_noise;
    FastNoiseLite cloud_noise;
    FastNoiseLite weather_noise; // For temporal weather variations
    FastNoiseLite pressure_noise; // For pressure systems and storm fronts
    
    explicit Impl(const WorldConfig& cfg) : config(cfg) {
        initialize_noise_generators();
    }
    
    void initialize_noise_generators() {
        // Terrain noise - creates continents, mountains, valleys
        terrain_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        terrain_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        terrain_noise.SetFractalOctaves(config.terrain_octaves);
        terrain_noise.SetFractalLacunarity(config.terrain_lacunarity);
        terrain_noise.SetFractalGain(config.terrain_gain);
        terrain_noise.SetFrequency(config.terrain_frequency * config.world_scale);
        terrain_noise.SetSeed(static_cast<int>(config.seed));
        
        // Moisture noise - affects precipitation and biomes
        moisture_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        moisture_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        moisture_noise.SetFractalOctaves(config.moisture_octaves);
        moisture_noise.SetFrequency(config.moisture_frequency * config.world_scale);
        moisture_noise.SetSeed(static_cast<int>(config.seed + 1000));
        
        // Temperature variation noise - adds local variations to base temperature
        temperature_variation_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        temperature_variation_noise.SetFrequency(0.003f * config.world_scale);
        temperature_variation_noise.SetSeed(static_cast<int>(config.seed + 2000));
        
        // Wind noise - creates wind patterns
        wind_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        wind_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        wind_noise.SetFractalOctaves(2);
        wind_noise.SetFrequency(0.002f * config.world_scale);
        wind_noise.SetSeed(static_cast<int>(config.seed + 3000));
        
        // River noise - adds variation to river placement
        river_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        river_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        river_noise.SetFractalOctaves(3);
        river_noise.SetFrequency(0.004f * config.world_scale);
        river_noise.SetSeed(static_cast<int>(config.seed + 4000));
        
        // Volcano noise - sparse cellular noise for volcano placement
        volcano_noise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
        volcano_noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
        volcano_noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
        volcano_noise.SetFrequency(0.008f * config.world_scale);
        volcano_noise.SetSeed(static_cast<int>(config.seed + 5000));
        
        // Coal noise - for coal deposits (sedimentary, lowland swamps)
        coal_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        coal_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        coal_noise.SetFractalOctaves(4);
        coal_noise.SetFrequency(0.003f * config.world_scale);
        coal_noise.SetSeed(static_cast<int>(config.seed + 6000));
        
        // Iron noise - for iron ore deposits (volcanic/ancient seabeds)
        iron_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        iron_noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
        iron_noise.SetFractalOctaves(3);
        iron_noise.SetFrequency(0.004f * config.world_scale);
        iron_noise.SetSeed(static_cast<int>(config.seed + 7000));
        
        // Oil noise - for oil deposits (sedimentary basins)
        oil_noise.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
        oil_noise.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
        oil_noise.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2);
        oil_noise.SetFrequency(0.003f * config.world_scale);
        oil_noise.SetSeed(static_cast<int>(config.seed + 4000));
        
        // Cloud noise - for atmospheric modeling
        cloud_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        cloud_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        cloud_noise.SetFractalOctaves(3);
        cloud_noise.SetFrequency(0.005f * config.world_scale);
        cloud_noise.SetSeed(static_cast<int>(config.seed + 5000));
        
        // Weather variation noise - for temporal changes in weather patterns
        weather_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        weather_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        weather_noise.SetFractalOctaves(2);
        weather_noise.SetFrequency(0.008f * config.world_scale); // Coarser for broad weather systems
        weather_noise.SetSeed(static_cast<int>(config.seed + 6000));
        
        // Pressure system noise - creates high/low pressure centers
        pressure_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        pressure_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
        pressure_noise.SetFractalOctaves(3);
        pressure_noise.SetFrequency(0.006f * config.world_scale); // Large-scale weather systems
        pressure_noise.SetSeed(static_cast<int>(config.seed + 7000));
    }
    
    // Convert geographic coordinates to world space for noise sampling
    void geo_to_world(float longitude, float latitude, float& x, float& y, float& z) const {
        // Convert to radians
        float lon_rad = longitude * 3.14159265359f / 180.0f;
        float lat_rad = latitude * 3.14159265359f / 180.0f;
        
        // Project onto sphere surface for seamless wrapping
        float r = 1000.0f; // Arbitrary sphere radius
        x = r * std::cos(lat_rad) * std::cos(lon_rad);
        y = r * std::cos(lat_rad) * std::sin(lon_rad);
        z = r * std::sin(lat_rad);
    }
    
    float get_terrain_height(float longitude, float latitude, float detail_level = 1.0f) const {
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Get base terrain noise (-1 to 1)
        float noise_value = terrain_noise.GetNoise(x, y, z);
        
        // Add finer detail layers when detail_level > 1.0
        if (detail_level > 1.0f) {
            // Add progressively finer octaves of detail
            float detail_contribution = 0.0f;
            float detail_amplitude = 0.3f; // Strength of detail layers
            float detail_frequency = 2.0f;
            
            // Add up to 3 detail octaves based on zoom level
            int detail_octaves = std::min(3, static_cast<int>(std::log2(detail_level)));
            
            for (int i = 0; i < detail_octaves; ++i) {
                float freq = detail_frequency * std::pow(2.0f, i);
                detail_contribution += terrain_noise.GetNoise(x * freq, y * freq, z * freq) * detail_amplitude;
                detail_amplitude *= 0.5f; // Each octave contributes less
            }
            
            // Blend detail based on zoom level
            float detail_blend = std::min(1.0f, (detail_level - 1.0f) / 4.0f);
            noise_value = noise_value * (1.0f - detail_blend * 0.3f) + detail_contribution * detail_blend;
        }
        
        // Apply power curve to create more ocean and distinct continents
        // Values below 0 are ocean, above 0 are land
        float shaped = noise_value;
        if (shaped < 0.0f) {
            // Ocean - steeper falloff
            shaped = shaped * shaped * -1.0f;
            shaped = shaped * shaped * -1.0f;
        } else {
            // Land - gentler slope with occasional peaks
            shaped = std::pow(shaped, 0.7f);
        }
        
        // Map to actual height range
        // Ocean: -4000m to 0m, Land: 0m to max_terrain_height
        float base_height = 0.0f;
        if (shaped < 0.0f) {
            base_height = shaped * 4000.0f;
        } else {
            base_height = shaped * config.max_terrain_height;
        }
        
        // Add volcanoes - only on land (independent of detail_level so always visible)
        if (base_height > 0.0f) {
            // Use cellular noise to find volcano centers
            float volcano_cell = volcano_noise.GetNoise(x, y, z);
            volcano_cell = (volcano_cell + 1.0f) * 0.5f; // Convert to 0-1
            
            // Only place volcanoes where cellular noise is very low (cell centers)
            if (volcano_cell < 0.2f) {
                // Calculate distance from volcano center
                // Lower cell value = closer to center
                float distance_factor = 1.0f - (volcano_cell / 0.2f);
                
                // Volcano height: cone shape with steep sides
                // Prefer higher elevations for volcanoes but can appear anywhere on land
                float elevation_preference = std::clamp((base_height - 300.0f) / 1500.0f, 0.2f, 1.0f);
                
                // Create cone shape: starts high at center, drops off with distance
                // Make them taller and more prominent
                float cone_height = distance_factor * distance_factor * distance_factor * 3000.0f; // Up to 3000m tall, steeper sides
                cone_height *= elevation_preference; 
                
                // Add a crater dip at the very center
                if (distance_factor > 0.85f) {
                    float crater_factor = (distance_factor - 0.85f) / 0.15f;
                    cone_height *= 1.0f - crater_factor * 0.4f; // 40% dip for crater
                }
                
                base_height += cone_height;
            }
        }
        
        return base_height;
    }
    
    bool is_volcano(float longitude, float latitude) const {
        float base_height = get_terrain_height(longitude, latitude);
        
        // No volcanoes in ocean
        if (base_height <= config.sea_level) {
            return false;
        }
        
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Check volcano noise
        float volcano_cell = volcano_noise.GetNoise(x, y, z);
        volcano_cell = (volcano_cell + 1.0f) * 0.5f;
        
        // Location is a volcano if within the cone radius
        return volcano_cell < 0.2f;
    }
    
    float get_coal_deposit(float longitude, float latitude) const {
        float terrain_height = get_terrain_height(longitude, latitude);
        
        // No coal in ocean or very high mountains
        if (terrain_height <= config.sea_level || terrain_height > 2000.0f) {
            return 0.0f;
        }
        
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Coal forms in ancient swamps - prefer wet, vegetated lowlands
        float coal_noise_value = coal_noise.GetNoise(x, y, z);
        coal_noise_value = (coal_noise_value + 1.0f) * 0.5f; // 0-1
        
        // Coal more likely in areas with:
        // - Moderate elevation (0-1500m - ancient swamps and forests)
        float elevation_factor = 0.0f;
        if (terrain_height >= 0.0f && terrain_height <= 1500.0f) {
            // Broad plateau from 0-1000m with gentle falloff
            if (terrain_height <= 1000.0f) {
                elevation_factor = 0.7f + (terrain_height / 2000.0f) * 0.3f; // 0.7 to 0.85
            } else {
                // Gentle decline above 1000m
                elevation_factor = 0.85f - ((terrain_height - 1000.0f) / 1000.0f) * 0.5f;
                elevation_factor = std::max(0.35f, elevation_factor);
            }
        }
        
        // - High historical precipitation (ancient forests/swamps)
        float altitude = std::max(terrain_height, 0.0f);
        float precip = get_precipitation(longitude, latitude, altitude);
        float moisture_factor = std::clamp(precip / 1500.0f, 0.2f, 1.0f); // Minimum 0.2
        
        // - Temperate to subtropical latitudes (20-60°) are best
        // Major coal deposits formed in Carboniferous period temperate swamps
        float abs_lat = std::abs(latitude);
        float lat_factor = 1.0f;
        if (abs_lat < 20.0f) {
            lat_factor = 0.7f + (abs_lat / 20.0f) * 0.3f; // 0.7-1.0 in tropics
        } else if (abs_lat <= 60.0f) {
            lat_factor = 1.0f; // Full factor in temperate zones
        } else {
            lat_factor = std::max(0.4f, 1.0f - (abs_lat - 60.0f) / 30.0f); // 1.0-0.4 toward poles
        }
        
        // Boost coal concentration overall
        float coal = coal_noise_value * elevation_factor * moisture_factor * lat_factor;
        coal = std::pow(coal, 0.7f); // Make deposits more concentrated
        return std::clamp(coal * 1.3f, 0.0f, 1.0f);
    }
    
    float get_iron_deposit(float longitude, float latitude) const {
        float terrain_height = get_terrain_height(longitude, latitude);
        
        // Iron can be anywhere on land
        if (terrain_height <= config.sea_level) {
            return 0.0f;
        }
        
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Iron forms in banded iron formations and volcanic regions
        float iron_noise_value = iron_noise.GetNoise(x, y, z);
        iron_noise_value = (iron_noise_value + 1.0f) * 0.5f; // 0-1
        
        // Iron more likely in:
        // - Volcanic regions (smaller bonus)
        float volcano_bonus = is_volcano(longitude, latitude) ? 0.25f : 0.0f;
        
        // - Ancient seabeds (low-mid elevation)
        float elevation_factor = 0.3f;
        if (terrain_height < 500.0f) {
            elevation_factor = 0.8f;
        } else if (terrain_height < 1000.0f) {
            elevation_factor = 0.6f;
        }
        
        // - Areas with geological activity (use ridged noise pattern)
        // Square the noise to make deposits more rare and concentrated
        float iron = (iron_noise_value * iron_noise_value) * elevation_factor + volcano_bonus;
        return std::clamp(iron * 0.8f, 0.0f, 1.0f);
    }
    
    float get_oil_deposit(float longitude, float latitude) const {
        float terrain_height = get_terrain_height(longitude, latitude);
        
        // Oil forms in sedimentary basins - prefer low to moderate elevations on land
        // Rare in deep ocean, focus on sedimentary basins on continents
        if (terrain_height < -200.0f || terrain_height > 1500.0f) {
            return 0.0f;
        }
        
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Oil deposits using cellular pattern (basin-like structures)
        float oil_noise_value = oil_noise.GetNoise(x, y, z);
        oil_noise_value = (oil_noise_value + 1.0f) * 0.5f; // 0-1
        
        // Oil more likely in:
        // - Sedimentary basins (lowlands, valleys, coastal plains)
        float elevation_factor = 0.0f;
        if (terrain_height >= 0.0f && terrain_height <= 1200.0f) {
            // Favor moderate elevations (100-800m) - inland sedimentary basins
            if (terrain_height < 100.0f) {
                elevation_factor = 0.5f; // Coastal areas - some deposits
            } else if (terrain_height <= 800.0f) {
                elevation_factor = 1.0f; // Sweet spot for sedimentary basins
            } else {
                elevation_factor = 1.0f - (terrain_height - 800.0f) / 400.0f;
                elevation_factor = std::max(0.3f, elevation_factor);
            }
        } else if (terrain_height >= -200.0f && terrain_height < 0.0f) {
            // Shallow ocean - offshore deposits (less common)
            elevation_factor = 0.4f;
        }
        
        // - Cellular pattern creates basin-like deposits
        // Make deposits concentrated but visible
        oil_noise_value = std::pow(oil_noise_value, 1.3f); // Less concentrated for more abundance
        
        float oil = oil_noise_value * elevation_factor;
        return std::clamp(oil * 1.2f, 0.0f, 1.0f);
    }
    
    float get_solar_angle(float longitude, float latitude, float current_time) const {
        // Calculate hour angle: how far the sun has moved from solar noon
        // Solar noon is at 12:00 at longitude 0°
        // Earth rotates 360° in 24 hours = 15° per hour
        float local_solar_time = current_time + (longitude / 15.0f);
        
        // Wrap to 0-24 range
        while (local_solar_time < 0.0f) local_solar_time += 24.0f;
        while (local_solar_time >= 24.0f) local_solar_time -= 24.0f;
        
        // Hour angle: 0° at solar noon (12:00), ±15° per hour
        float hour_angle = (local_solar_time - 12.0f) * 15.0f; // degrees
        
        // Solar declination (axial tilt effect based on day of year)
        // Earth's axial tilt is 23.44°
        // Day 0 = January 1, Day 172 = Summer solstice (June 21), Day 355 = Winter solstice (Dec 21)
        // Using simplified formula: declination peaks at solstices
        float day_angle = (config.day_of_year - 172) * 2.0f * M_PI / 365.0f;
        float solar_declination = 23.44f * std::cos(day_angle); // degrees
        
        // Convert to radians for trig
        float lat_rad = latitude * M_PI / 180.0f;
        float dec_rad = solar_declination * M_PI / 180.0f;
        float ha_rad = hour_angle * M_PI / 180.0f;
        
        // Solar elevation angle formula
        float sin_elevation = std::sin(lat_rad) * std::sin(dec_rad) +
                             std::cos(lat_rad) * std::cos(dec_rad) * std::cos(ha_rad);
        
        float elevation_angle = std::asin(std::clamp(sin_elevation, -1.0f, 1.0f)) * 180.0f / M_PI;
        
        return elevation_angle; // degrees above horizon (negative = below)
    }
    
    bool is_daylight(float longitude, float latitude, float current_time) const {
        return get_solar_angle(longitude, latitude, current_time) > 0.0f;
    }
    
    float get_insolation(float longitude, float latitude, float current_time) const {
        // Get solar angle
        float solar_angle = get_solar_angle(longitude, latitude, current_time);
        
        // No insolation if sun is below horizon
        if (solar_angle <= 0.0f) {
            return 0.0f;
        }
        
        // Solar constant at top of atmosphere
        const float SOLAR_CONSTANT = 1361.0f; // W/m²
        
        // Insolation based on solar angle (cosine law)
        float solar_angle_rad = solar_angle * M_PI / 180.0f;
        float base_insolation = SOLAR_CONSTANT * std::sin(solar_angle_rad);
        
        // Atmospheric attenuation (simplified - depends on air mass)
        // Air mass increases as sun gets lower in sky
        float air_mass = 1.0f / std::sin(solar_angle_rad);
        air_mass = std::clamp(air_mass, 1.0f, 10.0f);
        
        // Atmospheric transmission (simple exponential model)
        float atmospheric_transmission = std::pow(0.7f, air_mass);
        base_insolation *= atmospheric_transmission;
        
        // Cloud cover reduces insolation
        float terrain_height = get_terrain_height(longitude, latitude);
        float altitude = std::max(terrain_height, 0.0f);
        float cloud_density = get_cloud_density(longitude, latitude, altitude);
        
        // Clouds block 50-90% of radiation depending on density
        float cloud_factor = 1.0f - (cloud_density * 0.7f);
        
        float final_insolation = base_insolation * cloud_factor;
        
        return std::clamp(final_insolation, 0.0f, 1400.0f);
    }
    
    float get_cloud_density(float longitude, float latitude, float altitude) const {
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Get base noise pattern for cloud variation
        float noise = cloud_noise.GetNoise(x, y, z);
        noise = (noise + 1.0f) * 0.5f; // 0-1
        
        // Get environmental factors
        float humidity = get_humidity(longitude, latitude, altitude);
        float precip = get_precipitation(longitude, latitude, altitude);
        float temp = get_temperature(longitude, latitude, altitude);
        
        // Cloud density is heavily influenced by humidity
        float cloud_base = humidity * 0.8f + 0.2f * noise;
        
        // High precipitation areas have more clouds
        float precip_factor = std::clamp(precip / 2000.0f, 0.0f, 1.0f);
        cloud_base = cloud_base * 0.6f + precip_factor * 0.4f;
        
        // Temperature affects cloud formation
        // Cold air holds less moisture, hot air more
        float temp_factor = 1.0f;
        if (temp < -10.0f) {
            temp_factor = 0.5f; // Very cold - less clouds
        } else if (temp > 25.0f) {
            temp_factor = 1.2f; // Hot tropical - more clouds
        }
        
        float cloud_density = cloud_base * temp_factor;
        return std::clamp(cloud_density, 0.0f, 1.0f);
    }
    
    float get_vegetation_density(float longitude, float latitude, float altitude) const {
        // Get environmental factors
        float temp = get_temperature(longitude, latitude, altitude);
        float precip = get_precipitation(longitude, latitude, altitude);
        BiomeType biome = classify_biome(longitude, latitude, altitude);
        
        // Base density on biome type
        float base_density = 0.0f;
        switch (biome) {
            // Dense vegetation
            case BiomeType::TROPICAL_RAINFOREST:
                base_density = 1.0f;
                break;
            case BiomeType::TEMPERATE_RAINFOREST:
                base_density = 0.95f;
                break;
            case BiomeType::TROPICAL_SEASONAL_FOREST:
                base_density = 0.85f;
                break;
            case BiomeType::TEMPERATE_DECIDUOUS_FOREST:
                base_density = 0.80f;
                break;
            case BiomeType::TAIGA:
                base_density = 0.70f;
                break;
            case BiomeType::MOUNTAIN_FOREST:
                base_density = 0.65f;
                break;
                
            // Moderate vegetation
            case BiomeType::SAVANNA:
                base_density = 0.40f;
                break;
            case BiomeType::GRASSLAND:
                base_density = 0.30f;
                break;
                
            // Sparse vegetation
            case BiomeType::TUNDRA:
            case BiomeType::MOUNTAIN_TUNDRA:
                base_density = 0.15f;
                break;
                
            // Very sparse/no vegetation
            case BiomeType::DESERT:
            case BiomeType::COLD_DESERT:
                base_density = 0.05f;
                break;
            case BiomeType::ICE:
            case BiomeType::SNOW:
            case BiomeType::MOUNTAIN_PEAK:
                base_density = 0.0f;
                break;
            case BiomeType::OCEAN:
            case BiomeType::DEEP_OCEAN:
                base_density = 0.0f; // Could represent phytoplankton if needed
                break;
            case BiomeType::BEACH:
                base_density = 0.10f;
                break;
        }
        
        // Modulate by precipitation (more water = more plants)
        float precip_factor = std::clamp(precip / 1500.0f, 0.3f, 1.2f);
        base_density *= precip_factor;
        
        // Temperature affects growth
        float temp_factor = 1.0f;
        if (temp < -10.0f) {
            temp_factor = 0.3f; // Very cold limits growth
        } else if (temp < 0.0f) {
            temp_factor = 0.6f; // Cold reduces growth
        } else if (temp > 35.0f) {
            temp_factor = 0.7f; // Very hot without enough water limits growth
        }
        base_density *= temp_factor;
        
        // Elevation affects vegetation (higher = less vegetation)
        if (altitude > 3000.0f) {
            base_density *= 0.3f;
        } else if (altitude > 2000.0f) {
            base_density *= 0.6f;
        }
        
        // Add some noise variation for natural appearance
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        float noise = moisture_noise.GetNoise(x * 2.0f, y * 2.0f, z * 2.0f);
        noise = (noise + 1.0f) * 0.5f; // 0-1
        
        // Noise adds ±15% variation
        base_density *= (0.85f + noise * 0.3f);
        
        return std::clamp(base_density, 0.0f, 1.0f);
    }
    
    SoilType get_soil_type(float longitude, float latitude, float altitude) const {
        // No soil underwater, on ice, or extreme mountains
        if (altitude < 0.0f) {
            return SoilType::NONE;
        }
        if (altitude > 5000.0f) {
            return SoilType::ROCKY;
        }
        
        float temp = get_temperature(longitude, latitude, altitude);
        float precip = get_precipitation(longitude, latitude, altitude);
        BiomeType biome = classify_biome(longitude, latitude, altitude);
        
        // Ice/Snow - permafrost
        if (biome == BiomeType::ICE || biome == BiomeType::SNOW || 
            biome == BiomeType::MOUNTAIN_PEAK || temp < -5.0f) {
            return SoilType::PERMAFROST;
        }
        
        // Wetlands and rainforests - peat
        if (precip > 2000.0f && altitude < 100.0f) {
            return SoilType::PEAT;
        }
        
        // Mountains and high altitude - rocky
        if (altitude > 3000.0f || biome == BiomeType::MOUNTAIN_TUNDRA || 
            biome == BiomeType::MOUNTAIN_PEAK) {
            return SoilType::ROCKY;
        }
        
        // Deserts - sandy
        if (biome == BiomeType::DESERT || biome == BiomeType::COLD_DESERT) {
            return SoilType::SAND;
        }
        
        // Grasslands and temperate zones - loam (best soil)
        if (biome == BiomeType::GRASSLAND || biome == BiomeType::SAVANNA) {
            if (precip > 500.0f && precip < 1500.0f) {
                return SoilType::LOAM;
            }
        }
        
        // High precipitation temperate - clay
        if (precip > 1200.0f && temp > 5.0f && temp < 25.0f) {
            return SoilType::CLAY;
        }
        
        // Moderate precipitation - silt
        if (precip > 600.0f && precip < 1200.0f) {
            return SoilType::SILT;
        }
        
        // Default to sand for dry areas
        return SoilType::SAND;
    }
    
    float get_soil_fertility(float longitude, float latitude, float altitude) const {
        SoilType soil = get_soil_type(longitude, latitude, altitude);
        float temp = get_temperature(longitude, latitude, altitude);
        float precip = get_precipitation(longitude, latitude, altitude);
        float vegetation = get_vegetation_density(longitude, latitude, altitude);
        
        // Base fertility by soil type
        float base_fertility = 0.0f;
        switch (soil) {
            case SoilType::LOAM:
                base_fertility = 0.95f; // Ideal agricultural soil
                break;
            case SoilType::SILT:
                base_fertility = 0.75f; // Good fertility
                break;
            case SoilType::CLAY:
                base_fertility = 0.65f; // Nutrient-rich but poor drainage
                break;
            case SoilType::PEAT:
                base_fertility = 0.55f; // Organic but acidic
                break;
            case SoilType::SAND:
                base_fertility = 0.30f; // Poor nutrients and water retention
                break;
            case SoilType::ROCKY:
                base_fertility = 0.10f; // Very poor
                break;
            case SoilType::PERMAFROST:
                base_fertility = 0.05f; // Frozen, minimal growth
                break;
            case SoilType::NONE:
                base_fertility = 0.0f; // Water or ice
                break;
        }
        
        // Organic matter from vegetation increases fertility
        float organic_bonus = vegetation * 0.15f;
        base_fertility += organic_bonus;
        
        // Temperature affects decomposition and nutrient cycling
        float temp_factor = 1.0f;
        if (temp < 0.0f) {
            temp_factor = 0.3f; // Frozen = slow decomposition
        } else if (temp < 10.0f) {
            temp_factor = 0.6f; // Cool = slow decomposition
        } else if (temp > 30.0f) {
            temp_factor = 0.8f; // Very hot = rapid decomposition (nutrients lost)
        }
        base_fertility *= temp_factor;
        
        // Precipitation affects leaching
        // Too much washes away nutrients, too little limits growth
        float precip_factor = 1.0f;
        if (precip < 300.0f) {
            precip_factor = 0.4f; // Too dry
        } else if (precip > 2500.0f) {
            precip_factor = 0.7f; // Leached nutrients
        } else if (precip > 500.0f && precip < 1200.0f) {
            precip_factor = 1.1f; // Optimal range
        }
        base_fertility *= precip_factor;
        
        // Slope/altitude affects erosion (higher = more erosion = less fertile)
        if (altitude > 1500.0f) {
            base_fertility *= 0.7f;
        } else if (altitude > 500.0f) {
            base_fertility *= 0.85f;
        } else if (altitude < 50.0f && altitude > 0.0f) {
            base_fertility *= 1.1f; // River valleys and lowlands accumulate nutrients
        }
        
        return std::clamp(base_fertility, 0.0f, 1.0f);
    }
    
    float get_soil_ph(float longitude, float latitude, float altitude) const {
        SoilType soil = get_soil_type(longitude, latitude, altitude);
        float precip = get_precipitation(longitude, latitude, altitude);
        BiomeType biome = classify_biome(longitude, latitude, altitude);
        
        // Base pH by soil type
        float base_ph = 7.0f; // Neutral
        
        switch (soil) {
            case SoilType::PEAT:
                base_ph = 4.5f; // Acidic
                break;
            case SoilType::SAND:
                base_ph = 6.5f; // Slightly acidic
                break;
            case SoilType::CLAY:
                base_ph = 7.2f; // Slightly alkaline
                break;
            case SoilType::SILT:
                base_ph = 6.8f; // Near neutral
                break;
            case SoilType::LOAM:
                base_ph = 6.5f; // Slightly acidic (ideal for most plants)
                break;
            case SoilType::ROCKY:
                base_ph = 7.5f; // Alkaline
                break;
            case SoilType::PERMAFROST:
                base_ph = 6.0f; // Acidic
                break;
            case SoilType::NONE:
                base_ph = 7.0f; // Water is neutral
                break;
        }
        
        // High precipitation leaches bases, making soil more acidic
        if (precip > 1500.0f) {
            base_ph -= 0.8f;
        } else if (precip > 1000.0f) {
            base_ph -= 0.4f;
        }
        
        // Desert soils tend to be alkaline (less leaching)
        if (biome == BiomeType::DESERT || biome == BiomeType::COLD_DESERT) {
            base_ph += 0.5f;
        }
        
        // Forests make soil more acidic (leaf litter)
        if (biome == BiomeType::TAIGA || biome == BiomeType::TEMPERATE_DECIDUOUS_FOREST ||
            biome == BiomeType::TROPICAL_RAINFOREST) {
            base_ph -= 0.3f;
        }
        
        return std::clamp(base_ph, 4.0f, 9.0f);
    }
    
    float get_organic_matter(float longitude, float latitude, float altitude) const {
        SoilType soil = get_soil_type(longitude, latitude, altitude);
        float vegetation = get_vegetation_density(longitude, latitude, altitude);
        float temp = get_temperature(longitude, latitude, altitude);
        float precip = get_precipitation(longitude, latitude, altitude);
        
        // Base organic matter by soil type
        float base_organic = 0.0f;
        switch (soil) {
            case SoilType::PEAT:
                base_organic = 0.95f; // Mostly organic
                break;
            case SoilType::LOAM:
                base_organic = 0.40f; // Good organic content
                break;
            case SoilType::SILT:
                base_organic = 0.30f; // Moderate organic content
                break;
            case SoilType::CLAY:
                base_organic = 0.25f; // Retains some organic matter
                break;
            case SoilType::SAND:
                base_organic = 0.10f; // Poor retention
                break;
            case SoilType::ROCKY:
            case SoilType::PERMAFROST:
                base_organic = 0.05f; // Very little
                break;
            case SoilType::NONE:
                base_organic = 0.0f;
                break;
        }
        
        // Vegetation adds organic matter
        base_organic += vegetation * 0.4f;
        
        // Cold temperatures slow decomposition (preserves organic matter)
        if (temp < 0.0f) {
            base_organic *= 1.5f;
        } else if (temp < 10.0f) {
            base_organic *= 1.2f;
        } else if (temp > 25.0f) {
            base_organic *= 0.7f; // Rapid decomposition
        }
        
        // Moisture helps accumulate organic matter
        if (precip > 1500.0f) {
            base_organic *= 1.2f;
        } else if (precip < 500.0f) {
            base_organic *= 0.7f;
        }
        
        return std::clamp(base_organic, 0.0f, 1.0f);
    }
    
    float get_pressure_at_location(float longitude, float latitude, float altitude, float current_time) const {
        // Standard atmospheric pressure at altitude
        float altitude_pressure = get_air_pressure(altitude);
        
        // Add pressure variations from weather systems
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Move pressure systems with time
        float time_scaled = current_time * 0.1f;
        float pressure_variation = pressure_noise.GetNoise(x, y, z + time_scaled * 200.0f);
        
        // Pressure systems create ±25 mb variations
        // High pressure (1015-1040 mb) = clear weather
        // Low pressure (985-1010 mb) = storms
        float pressure_delta = pressure_variation * 25.0f;
        
        // Subtropical highs around 30° latitude
        float lat_factor = std::cos(latitude * 3.14159265359f / 180.0f * 2.0f);
        pressure_delta += lat_factor * 10.0f;
        
        return altitude_pressure + pressure_delta;
    }
    
    float get_pressure_gradient(float longitude, float latitude, float current_time) const {
        // Sample pressure at center and nearby points
        float center_pressure = get_pressure_at_location(longitude, latitude, 1000.0f, current_time);
        
        // Sample 1 degree away in cardinal directions
        float north_pressure = get_pressure_at_location(longitude, latitude + 1.0f, 1000.0f, current_time);
        float south_pressure = get_pressure_at_location(longitude, latitude - 1.0f, 1000.0f, current_time);
        float east_pressure = get_pressure_at_location(longitude + 1.0f, latitude, 1000.0f, current_time);
        float west_pressure = get_pressure_at_location(longitude - 1.0f, latitude, 1000.0f, current_time);
        
        // Calculate gradient magnitude (pressure change per degree)
        float dx = (east_pressure - west_pressure) / 2.0f;
        float dy = (north_pressure - south_pressure) / 2.0f;
        float gradient = std::sqrt(dx * dx + dy * dy);
        
        return gradient;
    }
    
    bool is_storm_front(float longitude, float latitude, float current_time) const {
        float gradient = get_pressure_gradient(longitude, latitude, current_time);
        
        // Storm fronts have very steep pressure gradients (> 5 mb per degree)
        // This represents major frontal systems with significant weather
        return gradient > 5.0f;
    }
    
    float get_moisture(float longitude, float latitude) const {
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Get moisture noise (0 to 1)
        float moisture = moisture_noise.GetNoise(x, y, z);
        moisture = (moisture + 1.0f) * 0.5f; // Convert from -1,1 to 0,1
        
        // Increase moisture near equator, decrease near poles
        float lat_factor = 1.0f - std::abs(latitude) / 90.0f;
        moisture = moisture * 0.7f + lat_factor * 0.3f;
        
        return std::clamp(moisture, 0.0f, 1.0f);
    }
    
    float get_base_temperature(float latitude, float altitude) const {
        // Base temperature from latitude
        float lat_factor = std::abs(latitude) / 90.0f; // 0 at equator, 1 at poles
        float base_temp = config.equator_temperature - 
                         (config.equator_temperature - config.pole_temperature) * lat_factor;
        
        // Adjust for altitude (temperature decreases with height)
        float altitude_adjustment = -(altitude / 1000.0f) * config.temperature_lapse_rate;
        
        return base_temp + altitude_adjustment;
    }
    
    float get_temperature(float longitude, float latitude, float altitude) const {
        float base_temp = get_base_temperature(latitude, altitude);
        
        // Add local variation
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        float variation = temperature_variation_noise.GetNoise(x, y, z) * 5.0f; // ±5°C variation
        
        return base_temp + variation;
    }
    
    float get_temperature_at_time(float longitude, float latitude, float altitude, float current_time) const {
        // Start with base temperature
        float base_temp = get_temperature(longitude, latitude, altitude);
        
        // Get insolation for solar heating effect
        float insolation = get_insolation(longitude, latitude, current_time);
        
        // Insolation effect: more sun = warmer (up to +15°C during peak day)
        // At 1000 W/m², add about +10°C; scales with insolation
        float solar_heating = (insolation / 1000.0f) * 10.0f;
        
        // Night cooling: when sun is down, temperature drops
        bool is_day = is_daylight(longitude, latitude, current_time);
        float night_cooling = 0.0f;
        if (!is_day) {
            // Night time - temperature drops by 5-15°C depending on cloud cover
            float cloud_density = get_cloud_density(longitude, latitude, altitude);
            // More clouds = less cooling (greenhouse effect)
            night_cooling = -5.0f - (10.0f * (1.0f - cloud_density));
        }
        
        // Cloud cooling during day: clouds block sun and reduce temperature
        float cloud_density = get_cloud_density(longitude, latitude, altitude);
        float cloud_effect = 0.0f;
        if (is_day) {
            // Dense clouds can reduce temperature by up to 5°C during day
            cloud_effect = -cloud_density * 5.0f;
        }
        
        // Combine all effects
        float dynamic_temp = base_temp + solar_heating + night_cooling + cloud_effect;
        
        // Daily temperature variation also depends on terrain and moisture
        // Deserts have high variation, humid areas have lower variation
        float humidity = get_humidity(longitude, latitude, altitude);
        float variation_damping = 0.5f + humidity * 0.5f; // Humidity reduces temperature swings
        
        // Apply damping to the dynamic components
        float dynamic_component = solar_heating + night_cooling + cloud_effect;
        dynamic_temp = base_temp + (dynamic_component * variation_damping);
        
        return dynamic_temp;
    }
    
    float get_precipitation(float longitude, float latitude, float altitude) const {
        float moisture = get_moisture(longitude, latitude);
        float temp = get_temperature(longitude, latitude, altitude);
        
        // Base precipitation from moisture
        float base_precip = moisture * 2000.0f; // 0 to 2000mm
        
        // Temperature affects precipitation capacity
        // Warmer air holds more moisture
        float temp_factor = std::clamp((temp + 10.0f) / 40.0f, 0.1f, 1.5f);
        base_precip *= temp_factor;
        
        // Altitude effect - mountains capture moisture (orographic precipitation)
        // But very high altitudes are dry
        float terrain_height = get_terrain_height(longitude, latitude);
        if (terrain_height > 500.0f && terrain_height < 3000.0f) {
            base_precip *= 1.3f; // Mountain slopes get more rain
        } else if (altitude > 4000.0f) {
            base_precip *= 0.5f; // Very high altitudes are dry
        }
        
        return std::clamp(base_precip, 0.0f, 4000.0f);
    }
    
    float get_current_precipitation(float longitude, float latitude, float altitude, float current_time) const {
        // Get base precipitation (annual average)
        float base_precip = get_precipitation(longitude, latitude, altitude);
        
        // Use time as 4th dimension for weather system movement
        // Time scale: weather systems move slowly (scaled by 0.01)
        float time_scaled = current_time * 0.1f; // Slower weather movement
        
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Sample weather noise with time component
        float weather_variation = weather_noise.GetNoise(x, y, z + time_scaled * 100.0f);
        weather_variation = (weather_variation + 1.0f) * 0.5f; // 0-1
        
        // Convert annual precipitation to instantaneous rate (0-1 scale)
        // High base precip = more likely to be raining right now
        float rain_probability = std::clamp(base_precip / 3000.0f, 0.0f, 0.8f);
        
        // Weather system determines if it's actually raining
        // Some variation around the probability
        float is_raining = (weather_variation < rain_probability + 0.3f) ? 1.0f : 0.0f;
        
        // Intensity varies with weather pattern
        float intensity = weather_variation * weather_variation; // 0-1, biased toward lighter rain
        
        return is_raining * intensity;
    }
    
    float get_air_pressure(float altitude) const {
        // Standard atmospheric pressure model
        // P = P0 * exp(-altitude / scale_height)
        const float P0 = 1013.25f; // Sea level pressure in millibars
        const float scale_height = 8500.0f; // Atmospheric scale height in meters
        
        return P0 * std::exp(-altitude / scale_height);
    }
    
    float get_humidity(float longitude, float latitude, float altitude) const {
        float moisture = get_moisture(longitude, latitude);
        float temp = get_temperature(longitude, latitude, altitude);
        
        // Relative humidity is affected by temperature
        // Cold air has higher relative humidity for same absolute moisture
        float temp_factor = 1.0f - std::clamp((temp - 10.0f) / 40.0f, 0.0f, 0.5f);
        
        float humidity = moisture * (0.5f + temp_factor);
        
        // Very high altitudes are dry
        if (altitude > 3000.0f) {
            humidity *= std::clamp(1.0f - (altitude - 3000.0f) / 5000.0f, 0.2f, 1.0f);
        }
        
        return std::clamp(humidity, 0.0f, 1.0f);
    }
    
    float get_wind_speed(float longitude, float latitude, float altitude) const {
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Base wind from noise
        float wind_base = wind_noise.GetNoise(x, y, z);
        wind_base = (wind_base + 1.0f) * 0.5f; // Convert to 0-1
        
        // Global wind patterns based on latitude
        float abs_lat = std::abs(latitude);
        float lat_wind = 0.0f;
        
        // Trade winds (0-30°), westerlies (30-60°), polar easterlies (60-90°)
        if (abs_lat < 30.0f) {
            lat_wind = 5.0f + (30.0f - abs_lat) / 30.0f * 3.0f; // 5-8 m/s
        } else if (abs_lat < 60.0f) {
            lat_wind = 7.0f + (abs_lat - 30.0f) / 30.0f * 5.0f; // 7-12 m/s
        } else {
            lat_wind = 6.0f + (90.0f - abs_lat) / 30.0f * 2.0f; // 6-8 m/s
        }
        
        // Terrain effect - mountains and elevation reduce wind at surface
        float terrain_height = get_terrain_height(longitude, latitude);
        float terrain_factor = 1.0f;
        if (altitude <= std::max(terrain_height, 0.0f) + 10.0f) {
            // Near surface, terrain roughness matters
            if (terrain_height > 1000.0f) {
                terrain_factor = 0.6f; // Mountains reduce wind
            } else if (terrain_height > 500.0f) {
                terrain_factor = 0.8f; // Hills reduce wind somewhat
            }
        } else {
            // Higher altitude = stronger winds
            terrain_factor = 1.0f + (altitude - terrain_height) / 5000.0f;
        }
        
        // Combine factors
        float wind_speed = (lat_wind * 0.6f + wind_base * 8.0f * 0.4f) * terrain_factor;
        
        return std::clamp(wind_speed, 0.0f, 30.0f);
    }
    
    float get_current_wind_speed(float longitude, float latitude, float altitude, float current_time) const {
        // Get base wind speed
        float base_wind = get_wind_speed(longitude, latitude, altitude);
        
        // Add temporal variation for gusts and weather systems
        float time_scaled = current_time * 0.2f; // Faster variation for wind
        
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Weather noise affects wind speed
        float weather_var = weather_noise.GetNoise(x, y, z + time_scaled * 50.0f);
        weather_var = (weather_var + 1.0f) * 0.5f; // 0-1
        
        // Wind can vary ±50% from base
        float variation_factor = 0.5f + weather_var;
        
        return std::clamp(base_wind * variation_factor, 0.0f, 40.0f);
    }
    
    float get_wind_direction(float longitude, float latitude, float altitude) const {
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Global wind patterns
        float abs_lat = std::abs(latitude);
        float base_direction = 0.0f;
        
        // Trade winds (easterlies 0-30°), westerlies (30-60°), polar easterlies (60-90°)
        if (abs_lat < 30.0f) {
            // Trade winds blow from east to west (270° = westward)
            base_direction = latitude >= 0.0f ? 240.0f : 120.0f; // NE in north, SE in south
        } else if (abs_lat < 60.0f) {
            // Westerlies blow from west to east (90° = eastward)
            base_direction = latitude >= 0.0f ? 60.0f : 300.0f; // SW in north, NW in south
        } else {
            // Polar easterlies
            base_direction = latitude >= 0.0f ? 120.0f : 240.0f; // NE in north, SE in south
        }
        
        // Add local variation from noise
        float noise_offset = wind_noise.GetNoise(x * 2.0f, y * 2.0f, z * 2.0f) * 60.0f;
        base_direction += noise_offset;
        
        // Normalize to 0-360
        while (base_direction < 0.0f) base_direction += 360.0f;
        while (base_direction >= 360.0f) base_direction -= 360.0f;
        
        return base_direction;
    }
    
    float get_current_wind_direction(float longitude, float latitude, float altitude, float current_time) const {
        // Get base wind direction
        float base_dir = get_wind_direction(longitude, latitude, altitude);
        
        // Add temporal variation for shifting winds
        float time_scaled = current_time * 0.15f;
        
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        
        // Weather system affects wind direction
        float weather_var = weather_noise.GetNoise(x * 1.5f, y * 1.5f, z * 1.5f + time_scaled * 30.0f);
        
        // Wind direction can shift ±45° from base
        float direction_shift = weather_var * 45.0f;
        float current_dir = base_dir + direction_shift;
        
        // Normalize to 0-360
        while (current_dir < 0.0f) current_dir += 360.0f;
        while (current_dir >= 360.0f) current_dir -= 360.0f;
        
        return current_dir;
    }
    
    // Calculate flow accumulation based on terrain gradient
    float get_flow_accumulation(float longitude, float latitude) const {
        float terrain_height = get_terrain_height(longitude, latitude);
        
        // No rivers in ocean or underwater
        if (terrain_height <= config.sea_level) {
            return 0.0f;
        }
        
        // Sample nearby elevations to estimate gradient and flow direction
        const float sample_dist = 0.1f; // degrees
        float h_north = get_terrain_height(longitude, latitude + sample_dist);
        float h_south = get_terrain_height(longitude, latitude - sample_dist);
        float h_east = get_terrain_height(longitude + sample_dist, latitude);
        float h_west = get_terrain_height(longitude - sample_dist, latitude);
        
        // Calculate how much this location is a "sink" (lower than surroundings)
        float avg_neighbor = (h_north + h_south + h_east + h_west) / 4.0f;
        float height_diff = avg_neighbor - terrain_height;
        
        // Positive height_diff means we're in a valley/channel
        float valley_factor = std::clamp(height_diff / 50.0f, 0.0f, 1.0f);
        
        // Calculate gradient magnitude - steeper = more defined channels
        float grad_ns = std::abs(h_north - h_south) / (2.0f * sample_dist);
        float grad_ew = std::abs(h_east - h_west) / (2.0f * sample_dist);
        float gradient = std::sqrt(grad_ns * grad_ns + grad_ew * grad_ew);
        float gradient_factor = std::clamp(gradient / 500.0f, 0.2f, 1.5f);
        
        // Get precipitation - more water = more flow
        float altitude = std::max(terrain_height, 0.0f);
        float precip = get_precipitation(longitude, latitude, altitude);
        float precip_factor = std::clamp(precip / 1500.0f, 0.1f, 1.5f);
        
        // Add noise variation for natural-looking river networks
        float x, y, z;
        geo_to_world(longitude, latitude, x, y, z);
        float noise = river_noise.GetNoise(x, y, z);
        noise = (noise + 1.0f) * 0.5f; // 0-1
        
        // Boost noise influence to create more rivers
        float noise_factor = noise * noise; // Square for more variation
        
        // Combine factors: valleys + precipitation + noise + gradient
        float flow = valley_factor * 0.4f + precip_factor * 0.25f + noise_factor * 0.35f;
        flow *= gradient_factor;
        
        // Elevation influence - rivers more common at mid-elevations
        if (terrain_height < 100.0f) {
            flow *= 2.0f; // Near sea level - wide river plains and deltas
        } else if (terrain_height < 500.0f) {
            flow *= 1.3f; // Low elevation - good for rivers
        } else if (terrain_height > 3000.0f) {
            flow *= 0.4f; // High mountains - fewer/smaller rivers
        }
        
        return std::clamp(flow, 0.0f, 1.0f);
    }
    
    bool is_river(float longitude, float latitude) const {
        float terrain_height = get_terrain_height(longitude, latitude);
        
        // No rivers in ocean
        if (terrain_height <= config.sea_level) {
            return false;
        }
        
        float flow = get_flow_accumulation(longitude, latitude);
        // Lower threshold for river presence to show more rivers
        return flow > 0.4f;
    }
    
    float get_river_width(float longitude, float latitude) const {
        float terrain_height = get_terrain_height(longitude, latitude);
        
        // No rivers in ocean
        if (terrain_height <= config.sea_level) {
            return 0.0f;
        }
        
        float flow = get_flow_accumulation(longitude, latitude);
        
        if (flow < 0.4f) {
            return 0.0f; // No river
        }
        
        // Width increases with flow accumulation
        float base_width = (flow - 0.4f) / 0.6f; // 0-1 for flow 0.4-1.0
        
        // Use terrain height for elevation influence (already have it)
        float altitude = std::max(terrain_height, 0.0f);
        
        // Rivers get wider at lower elevations (approaching sea)
        float elevation_factor = 1.0f;
        if (terrain_height < 500.0f) {
            elevation_factor = 2.0f + (500.0f - terrain_height) / 500.0f * 3.0f; // 2-5x wider
        }
        
        // Get precipitation for flow volume
        float precip = get_precipitation(longitude, latitude, altitude);
        float precip_factor = 0.5f + std::clamp(precip / 2000.0f, 0.0f, 1.0f) * 0.5f; // 0.5-1.0
        
        // Calculate width: 5-200 meters
        float width = 5.0f + base_width * base_width * 40.0f * elevation_factor * precip_factor;
        
        return std::clamp(width, 0.0f, 500.0f);
    }
    
    BiomeType classify_biome(float longitude, float latitude, float altitude) const {
        float terrain_height = get_terrain_height(longitude, latitude);
        
        // Ocean biomes
        if (terrain_height < config.sea_level) {
            if (terrain_height < -1000.0f) {
                return BiomeType::DEEP_OCEAN;
            }
            return BiomeType::OCEAN;
        }
        
        // Beach transition
        if (terrain_height < 5.0f) {
            return BiomeType::BEACH;
        }
        
        float temp = get_temperature(longitude, latitude, altitude);
        float moisture = get_moisture(longitude, latitude);
        
        // Snow and ice
        if (temp < -15.0f) {
            if (terrain_height < 100.0f) {
                return BiomeType::ICE;
            }
            return BiomeType::SNOW;
        }
        
        // High mountain biomes
        if (altitude > 4000.0f) {
            return BiomeType::MOUNTAIN_PEAK;
        } else if (altitude > 2500.0f) {
            if (temp < 0.0f) {
                return BiomeType::MOUNTAIN_TUNDRA;
            }
            return BiomeType::MOUNTAIN_FOREST;
        }
        
        // Whittaker diagram classification
        // Based on temperature and moisture
        
        // Cold (< 0°C)
        if (temp < 0.0f) {
            if (moisture < 0.3f) {
                return BiomeType::COLD_DESERT;
            }
            return BiomeType::TUNDRA;
        }
        
        // Cool (0-10°C)
        if (temp < 10.0f) {
            if (moisture < 0.3f) {
                return BiomeType::COLD_DESERT;
            } else if (moisture < 0.6f) {
                return BiomeType::GRASSLAND;
            }
            return BiomeType::TAIGA;
        }
        
        // Temperate (10-20°C)
        if (temp < 20.0f) {
            if (moisture < 0.3f) {
                return BiomeType::GRASSLAND;
            } else if (moisture < 0.6f) {
                return BiomeType::TEMPERATE_DECIDUOUS_FOREST;
            }
            return BiomeType::TEMPERATE_RAINFOREST;
        }
        
        // Hot (> 20°C)
        if (moisture < 0.2f) {
            return BiomeType::DESERT;
        } else if (moisture < 0.5f) {
            return BiomeType::SAVANNA;
        } else if (moisture < 0.7f) {
            return BiomeType::TROPICAL_SEASONAL_FOREST;
        }
        return BiomeType::TROPICAL_RAINFOREST;
    }
};

// World implementation

World::World() : pimpl_(std::make_unique<Impl>(WorldConfig{})) {}

World::World(const WorldConfig& config) : pimpl_(std::make_unique<Impl>(config)) {}

World::~World() = default;

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

BiomeType World::get_biome(float longitude, float latitude, float altitude) const {
    return pimpl_->classify_biome(longitude, latitude, altitude);
}

float World::get_temperature(float longitude, float latitude, float altitude) const {
    return pimpl_->get_temperature(longitude, latitude, altitude);
}

float World::get_temperature_at_time(float longitude, float latitude, float altitude, float current_time) const {
    return pimpl_->get_temperature_at_time(longitude, latitude, altitude, current_time);
}

float World::get_terrain_height(float longitude, float latitude, float detail_level) const {
    return pimpl_->get_terrain_height(longitude, latitude, detail_level);
}

float World::get_precipitation(float longitude, float latitude, float altitude) const {
    return pimpl_->get_precipitation(longitude, latitude, altitude);
}

float World::get_current_precipitation(float longitude, float latitude, float altitude, float current_time) const {
    return pimpl_->get_current_precipitation(longitude, latitude, altitude, current_time);
}

PrecipitationType World::get_precipitation_type(float longitude, float latitude, float altitude) const {
    float temp = get_temperature(longitude, latitude, altitude);
    float precip = get_precipitation(longitude, latitude, altitude);
    
    if (precip < 100.0f) {
        return PrecipitationType::NONE;
    }
    
    if (temp < -2.0f) {
        return PrecipitationType::SNOW;
    } else if (temp < 2.0f) {
        return PrecipitationType::SLEET;
    }
    return PrecipitationType::RAIN;
}

float World::get_air_pressure(float longitude, float latitude, float altitude) const {
    (void)longitude; // Unused - pressure mainly depends on altitude
    (void)latitude;
    return pimpl_->get_air_pressure(altitude);
}

float World::get_humidity(float longitude, float latitude, float altitude) const {
    return pimpl_->get_humidity(longitude, latitude, altitude);
}

float World::get_wind_speed(float longitude, float latitude, float altitude) const {
    return pimpl_->get_wind_speed(longitude, latitude, altitude);
}

float World::get_current_wind_speed(float longitude, float latitude, float altitude, float current_time) const {
    return pimpl_->get_current_wind_speed(longitude, latitude, altitude, current_time);
}

float World::get_wind_direction(float longitude, float latitude, float altitude) const {
    return pimpl_->get_wind_direction(longitude, latitude, altitude);
}

float World::get_current_wind_direction(float longitude, float latitude, float altitude, float current_time) const {
    return pimpl_->get_current_wind_direction(longitude, latitude, altitude, current_time);
}

bool World::is_river(float longitude, float latitude) const {
    return pimpl_->is_river(longitude, latitude);
}

float World::get_river_width(float longitude, float latitude) const {
    return pimpl_->get_river_width(longitude, latitude);
}

float World::get_flow_accumulation(float longitude, float latitude) const {
    return pimpl_->get_flow_accumulation(longitude, latitude);
}

bool World::is_volcano(float longitude, float latitude) const {
    return pimpl_->is_volcano(longitude, latitude);
}

float World::get_coal_deposit(float longitude, float latitude) const {
    return pimpl_->get_coal_deposit(longitude, latitude);
}

float World::get_iron_deposit(float longitude, float latitude) const {
    return pimpl_->get_iron_deposit(longitude, latitude);
}

float World::get_oil_deposit(float longitude, float latitude) const {
    return pimpl_->get_oil_deposit(longitude, latitude);
}

float World::get_insolation(float longitude, float latitude, float current_time) const {
    return pimpl_->get_insolation(longitude, latitude, current_time);
}

bool World::is_daylight(float longitude, float latitude, float current_time) const {
    return pimpl_->is_daylight(longitude, latitude, current_time);
}

float World::get_solar_angle(float longitude, float latitude, float current_time) const {
    return pimpl_->get_solar_angle(longitude, latitude, current_time);
}

float World::get_vegetation_density(float longitude, float latitude, float altitude) const {
    return pimpl_->get_vegetation_density(longitude, latitude, altitude);
}

SoilType World::get_soil_type(float longitude, float latitude, float altitude) const {
    return pimpl_->get_soil_type(longitude, latitude, altitude);
}

float World::get_soil_fertility(float longitude, float latitude, float altitude) const {
    return pimpl_->get_soil_fertility(longitude, latitude, altitude);
}

float World::get_soil_ph(float longitude, float latitude, float altitude) const {
    return pimpl_->get_soil_ph(longitude, latitude, altitude);
}

float World::get_organic_matter(float longitude, float latitude, float altitude) const {
    return pimpl_->get_organic_matter(longitude, latitude, altitude);
}

float World::get_pressure_at_location(float longitude, float latitude, float altitude, float current_time) const {
    return pimpl_->get_pressure_at_location(longitude, latitude, altitude, current_time);
}

float World::get_pressure_gradient(float longitude, float latitude, float current_time) const {
    return pimpl_->get_pressure_gradient(longitude, latitude, current_time);
}

bool World::is_storm_front(float longitude, float latitude, float current_time) const {
    return pimpl_->is_storm_front(longitude, latitude, current_time);
}

BatchResult World::batch_query(const std::vector<Location>& locations,
                               const std::vector<DataType>& data_types) const {
    BatchResult result;
    result.count = locations.size();
    
    if (locations.empty() || data_types.empty()) {
        return result;
    }
    
    // Pre-allocate vectors for requested data types
    for (DataType type : data_types) {
        switch (type) {
            case DataType::TERRAIN_HEIGHT:
                result.terrain_height.reserve(result.count);
                break;
            case DataType::TEMPERATURE:
            case DataType::TEMPERATURE_AT_TIME:
                result.temperature.reserve(result.count);
                break;
            case DataType::BIOME:
                result.biome.reserve(result.count);
                break;
            case DataType::PRECIPITATION:
            case DataType::CURRENT_PRECIPITATION:
                result.precipitation.reserve(result.count);
                break;
            case DataType::PRECIPITATION_TYPE:
                result.precipitation_type.reserve(result.count);
                break;
            case DataType::AIR_PRESSURE:
            case DataType::PRESSURE_AT_LOCATION:
                result.air_pressure.reserve(result.count);
                break;
            case DataType::HUMIDITY:
                result.humidity.reserve(result.count);
                break;
            case DataType::WIND_SPEED:
            case DataType::CURRENT_WIND_SPEED:
                result.wind_speed.reserve(result.count);
                break;
            case DataType::WIND_DIRECTION:
            case DataType::CURRENT_WIND_DIRECTION:
                result.wind_direction.reserve(result.count);
                break;
            case DataType::IS_RIVER:
                result.is_river.reserve(result.count);
                break;
            case DataType::RIVER_WIDTH:
                result.river_width.reserve(result.count);
                break;
            case DataType::FLOW_ACCUMULATION:
                result.flow_accumulation.reserve(result.count);
                break;
            case DataType::IS_VOLCANO:
                result.is_volcano.reserve(result.count);
                break;
            case DataType::COAL_DEPOSIT:
                result.coal_deposit.reserve(result.count);
                break;
            case DataType::IRON_DEPOSIT:
                result.iron_deposit.reserve(result.count);
                break;
            case DataType::OIL_DEPOSIT:
                result.oil_deposit.reserve(result.count);
                break;
            case DataType::INSOLATION:
                result.insolation.reserve(result.count);
                break;
            case DataType::IS_DAYLIGHT:
                result.is_daylight.reserve(result.count);
                break;
            case DataType::SOLAR_ANGLE:
                result.solar_angle.reserve(result.count);
                break;
            case DataType::VEGETATION_DENSITY:
                result.vegetation_density.reserve(result.count);
                break;
            case DataType::SOIL_TYPE:
                result.soil_type.reserve(result.count);
                break;
            case DataType::SOIL_FERTILITY:
                result.soil_fertility.reserve(result.count);
                break;
            case DataType::SOIL_PH:
                result.soil_ph.reserve(result.count);
                break;
            case DataType::ORGANIC_MATTER:
                result.organic_matter.reserve(result.count);
                break;
            case DataType::PRESSURE_GRADIENT:
                result.pressure_gradient.reserve(result.count);
                break;
            case DataType::IS_STORM_FRONT:
                result.is_storm_front.reserve(result.count);
                break;
        }
    }
    
    // Process all locations
    for (const Location& loc : locations) {
        // Cache commonly needed values
        float terrain_height = 0.0f;
        float altitude = loc.altitude;
        bool terrain_computed = false;
        
        // If altitude is needed but not provided, compute terrain height once
        auto ensure_terrain = [&]() {
            if (!terrain_computed) {
                terrain_height = pimpl_->get_terrain_height(loc.longitude, loc.latitude, loc.detail_level);
                if (loc.altitude == 0.0f) {
                    altitude = std::max(terrain_height, 0.0f);
                }
                terrain_computed = true;
            }
        };
        
        // Query each requested data type
        for (DataType type : data_types) {
            switch (type) {
                case DataType::TERRAIN_HEIGHT:
                    ensure_terrain();
                    result.terrain_height.push_back(terrain_height);
                    break;
                    
                case DataType::TEMPERATURE:
                    ensure_terrain();
                    result.temperature.push_back(pimpl_->get_temperature(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::TEMPERATURE_AT_TIME:
                    ensure_terrain();
                    result.temperature.push_back(pimpl_->get_temperature_at_time(loc.longitude, loc.latitude, altitude, loc.current_time));
                    break;
                    
                case DataType::BIOME:
                    ensure_terrain();
                    result.biome.push_back(pimpl_->classify_biome(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::PRECIPITATION:
                    ensure_terrain();
                    result.precipitation.push_back(pimpl_->get_precipitation(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::CURRENT_PRECIPITATION:
                    ensure_terrain();
                    result.precipitation.push_back(pimpl_->get_current_precipitation(loc.longitude, loc.latitude, altitude, loc.current_time));
                    break;
                    
                case DataType::PRECIPITATION_TYPE: {
                    ensure_terrain();
                    float temp = pimpl_->get_temperature(loc.longitude, loc.latitude, altitude);
                    float precip = pimpl_->get_precipitation(loc.longitude, loc.latitude, altitude);
                    PrecipitationType ptype = PrecipitationType::NONE;
                    if (precip >= 100.0f) {
                        if (temp < -2.0f) {
                            ptype = PrecipitationType::SNOW;
                        } else if (temp < 2.0f) {
                            ptype = PrecipitationType::SLEET;
                        } else {
                            ptype = PrecipitationType::RAIN;
                        }
                    }
                    result.precipitation_type.push_back(ptype);
                    break;
                }
                    
                case DataType::AIR_PRESSURE:
                    result.air_pressure.push_back(pimpl_->get_air_pressure(altitude));
                    break;
                    
                case DataType::HUMIDITY:
                    ensure_terrain();
                    result.humidity.push_back(pimpl_->get_humidity(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::WIND_SPEED:
                    ensure_terrain();
                    result.wind_speed.push_back(pimpl_->get_wind_speed(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::CURRENT_WIND_SPEED:
                    ensure_terrain();
                    result.wind_speed.push_back(pimpl_->get_current_wind_speed(loc.longitude, loc.latitude, altitude, loc.current_time));
                    break;
                    
                case DataType::WIND_DIRECTION:
                    ensure_terrain();
                    result.wind_direction.push_back(pimpl_->get_wind_direction(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::CURRENT_WIND_DIRECTION:
                    ensure_terrain();
                    result.wind_direction.push_back(pimpl_->get_current_wind_direction(loc.longitude, loc.latitude, altitude, loc.current_time));
                    break;
                    
                case DataType::IS_RIVER:
                    result.is_river.push_back(pimpl_->is_river(loc.longitude, loc.latitude));
                    break;
                    
                case DataType::RIVER_WIDTH:
                    result.river_width.push_back(pimpl_->get_river_width(loc.longitude, loc.latitude));
                    break;
                    
                case DataType::FLOW_ACCUMULATION:
                    result.flow_accumulation.push_back(pimpl_->get_flow_accumulation(loc.longitude, loc.latitude));
                    break;
                    
                case DataType::IS_VOLCANO:
                    result.is_volcano.push_back(pimpl_->is_volcano(loc.longitude, loc.latitude));
                    break;
                    
                case DataType::COAL_DEPOSIT:
                    result.coal_deposit.push_back(pimpl_->get_coal_deposit(loc.longitude, loc.latitude));
                    break;
                    
                case DataType::IRON_DEPOSIT:
                    result.iron_deposit.push_back(pimpl_->get_iron_deposit(loc.longitude, loc.latitude));
                    break;
                    
                case DataType::OIL_DEPOSIT:
                    result.oil_deposit.push_back(pimpl_->get_oil_deposit(loc.longitude, loc.latitude));
                    break;
                    
                case DataType::INSOLATION:
                    result.insolation.push_back(pimpl_->get_insolation(loc.longitude, loc.latitude, loc.current_time));
                    break;
                    
                case DataType::IS_DAYLIGHT:
                    result.is_daylight.push_back(pimpl_->is_daylight(loc.longitude, loc.latitude, loc.current_time));
                    break;
                    
                case DataType::SOLAR_ANGLE:
                    result.solar_angle.push_back(pimpl_->get_solar_angle(loc.longitude, loc.latitude, loc.current_time));
                    break;
                    
                case DataType::VEGETATION_DENSITY:
                    ensure_terrain();
                    result.vegetation_density.push_back(pimpl_->get_vegetation_density(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::SOIL_TYPE:
                    ensure_terrain();
                    result.soil_type.push_back(pimpl_->get_soil_type(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::SOIL_FERTILITY:
                    ensure_terrain();
                    result.soil_fertility.push_back(pimpl_->get_soil_fertility(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::SOIL_PH:
                    ensure_terrain();
                    result.soil_ph.push_back(pimpl_->get_soil_ph(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::ORGANIC_MATTER:
                    ensure_terrain();
                    result.organic_matter.push_back(pimpl_->get_organic_matter(loc.longitude, loc.latitude, altitude));
                    break;
                    
                case DataType::PRESSURE_AT_LOCATION:
                    ensure_terrain();
                    result.pressure_at_location.push_back(pimpl_->get_pressure_at_location(loc.longitude, loc.latitude, altitude, loc.current_time));
                    break;
                    
                case DataType::PRESSURE_GRADIENT:
                    result.pressure_gradient.push_back(pimpl_->get_pressure_gradient(loc.longitude, loc.latitude, loc.current_time));
                    break;
                    
                case DataType::IS_STORM_FRONT:
                    result.is_storm_front.push_back(pimpl_->is_storm_front(loc.longitude, loc.latitude, loc.current_time));
                    break;
            }
        }
    }
    
    return result;
}

void World::set_config(const WorldConfig& config) {
    pimpl_->config = config;
    pimpl_->initialize_noise_generators();
}

const WorldConfig& World::get_config() const {
    return pimpl_->config;
}

const char* biome_to_string(BiomeType biome) {
    switch (biome) {
        case BiomeType::TUNDRA: return "Tundra";
        case BiomeType::TAIGA: return "Taiga";
        case BiomeType::GRASSLAND: return "Grassland";
        case BiomeType::TEMPERATE_DECIDUOUS_FOREST: return "Temperate Deciduous Forest";
        case BiomeType::TEMPERATE_RAINFOREST: return "Temperate Rainforest";
        case BiomeType::SAVANNA: return "Savanna";
        case BiomeType::TROPICAL_SEASONAL_FOREST: return "Tropical Seasonal Forest";
        case BiomeType::TROPICAL_RAINFOREST: return "Tropical Rainforest";
        case BiomeType::COLD_DESERT: return "Cold Desert";
        case BiomeType::DESERT: return "Desert";
        case BiomeType::OCEAN: return "Ocean";
        case BiomeType::DEEP_OCEAN: return "Deep Ocean";
        case BiomeType::BEACH: return "Beach";
        case BiomeType::SNOW: return "Snow";
        case BiomeType::ICE: return "Ice";
        case BiomeType::MOUNTAIN_TUNDRA: return "Mountain Tundra";
        case BiomeType::MOUNTAIN_FOREST: return "Mountain Forest";
        case BiomeType::MOUNTAIN_PEAK: return "Mountain Peak";
        default: return "Unknown";
    }
}

const char* soil_to_string(SoilType soil) {
    switch (soil) {
        case SoilType::CLAY: return "Clay";
        case SoilType::SILT: return "Silt";
        case SoilType::SAND: return "Sand";
        case SoilType::LOAM: return "Loam";
        case SoilType::PEAT: return "Peat";
        case SoilType::ROCKY: return "Rocky";
        case SoilType::PERMAFROST: return "Permafrost";
        case SoilType::NONE: return "None";
        default: return "Unknown";
    }
}

} // namespace rworld
#endif // _RWORLD_IMPLEMENTATION
#endif // RWORLD_WORLD_H
