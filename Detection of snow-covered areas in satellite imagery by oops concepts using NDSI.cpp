#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <gdal_priv.h>
#include <cpl_error.h>

// -------------------------------
// Base Class: RasterProcessor
// -------------------------------
class RasterProcessor {
protected:
    std::string inputFile;
    std::string outputFile;
    int width{}, height{}, numBands{};
    double geoTransform[6]{};
    std::string projection;

protected:
    virtual bool loadData() = 0;

public:
    RasterProcessor(const std::string& inFile, const std::string& outFile)
        : inputFile(inFile), outputFile(outFile) {}

    virtual ~RasterProcessor() = default;
    virtual void process() = 0;
};

// ---------------------------------------
// Derived Class: SnowDetector
// ---------------------------------------
class SnowDetector : public RasterProcessor {
private:
    static constexpr int GREEN_BAND = 3; 
    static constexpr int SWIR_BAND = 11; 
    static constexpr float NDSI_THRESHOLD = 0.4f;

    std::vector<float> greenBand, swir1Band;
    std::vector<unsigned char> redBand, blueBand;

    void writeOutput() {
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!driver) { std::cerr << "GTiff driver not available.\n"; return; }

        GDALDataset* outDataset = driver->Create(outputFile.c_str(), width, height, 3, GDT_Byte, nullptr);
        if (!outDataset) { std::cerr << "Cannot create output dataset.\n"; return; }

        outDataset->SetGeoTransform(geoTransform);
        outDataset->SetProjection(projection.c_str());

        GDALRasterBand* red = outDataset->GetRasterBand(1);
        GDALRasterBand* green = outDataset->GetRasterBand(2);
        GDALRasterBand* blue = outDataset->GetRasterBand(3);

        std::vector<unsigned char> zero_vec(width * height, 0);

        red->RasterIO(GF_Write, 0, 0, width, height, redBand.data(), width, height, GDT_Byte, 0, 0);
        green->RasterIO(GF_Write, 0, 0, width, height, zero_vec.data(), width, height, GDT_Byte, 0, 0);
        blue->RasterIO(GF_Write, 0, 0, width, height, blueBand.data(), width, height, GDT_Byte, 0, 0);

        GDALClose(outDataset);
        std::cout << "Output written: " << outputFile << " (Blue=Snow, Red=Non-snow)\n";
    }

public:
    SnowDetector(const std::string& inFile, const std::string& outFile)
        : RasterProcessor(inFile, outFile) {}

    bool loadData() override {
        GDALDataset* dataset = (GDALDataset*)GDALOpen(inputFile.c_str(), GA_ReadOnly);
        if (!dataset) { std::cerr << "Cannot open input file.\n"; return false; }

        numBands = dataset->GetRasterCount();
        if (numBands < SWIR_BAND) {
            std::cerr << "Insufficient bands.\n";
            GDALClose(dataset);
            return false;
        }

        dataset->GetGeoTransform(geoTransform);
        projection = dataset->GetProjectionRef();

        GDALRasterBand* green = dataset->GetRasterBand(GREEN_BAND);
        GDALRasterBand* swir1 = dataset->GetRasterBand(SWIR_BAND);

        width = green->GetXSize();
        height = green->GetYSize();
        size_t numPixels = (size_t)width * height;

        greenBand.resize(numPixels);
        swir1Band.resize(numPixels);

        green->RasterIO(GF_Read, 0, 0, width, height, greenBand.data(), width, height, GDT_Float32, 0, 0);
        swir1->RasterIO(GF_Read, 0, 0, width, height, swir1Band.data(), width, height, GDT_Float32, 0, 0);

        GDALClose(dataset);
        return true;
    }

    void process() override {
        if (!loadData()) { std::cerr << "Error loading data.\n"; return; }

        size_t numPixels = (size_t)width * height;
        redBand.assign(numPixels, 0);
        blueBand.assign(numPixels, 0);

        for (size_t i = 0; i < numPixels; ++i) {
            float g = greenBand[i], s = swir1Band[i];
            float ndsi = (g + s != 0.0f) ? (g - s) / (g + s) : 0.0f;

            if (ndsi > NDSI_THRESHOLD)
                blueBand[i] = 255; 
            else
                redBand[i] = 255;  
        }

        writeOutput();
    }
};

// -------------------------------
// Main Function
// -------------------------------
int main() {
    GDALAllRegister();

    std::string inputPath = "input_sentinel2.tif";
    std::string outputPath = "snow_only_colored.tif";

    auto processor = std::make_unique<SnowDetector>(inputPath, outputPath);
    processor->process();

    std::cout << "Program completed successfully.\n";
    return 0;
}
