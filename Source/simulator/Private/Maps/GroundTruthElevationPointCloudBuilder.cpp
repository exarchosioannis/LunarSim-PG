#include "Maps/GroundTruthElevationPointCloudBuilder.h"

#include "sensor_msgs/msg/point_field.hpp"

namespace
{
	void AddFloatField(sensor_msgs::msg::PointCloud2& Cloud, const char* Name, uint32 Offset)
	{
		sensor_msgs::msg::PointField Field;
		Field.name = Name;
		Field.offset = Offset;
		Field.datatype = sensor_msgs::msg::PointField::FLOAT32;
		Field.count = 1;

		Cloud.fields.push_back(Field);
	}

	void WriteFloat(TArray<uint8>& Buffer, int32 Offset, float Value)
	{
		if (Offset < 0 || Offset + static_cast<int32>(sizeof(float)) > Buffer.Num())
		{
			return;
		}

		FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(float));
	}
}

sensor_msgs::msg::PointCloud2 FGroundTruthElevationPointCloudBuilder::BuildElevationPointCloud(
	const FGroundTruthElevationPointCloudBuildInfo& Info,
	const TArray<float>& ElevationDataMeters)
{
	sensor_msgs::msg::PointCloud2 Cloud;

	Cloud.header.frame_id = TCHAR_TO_UTF8(*Info.FrameId);

	if (Info.Width == 0 || Info.Height == 0 || Info.ResolutionMeters <= 0.0f)
	{
		return Cloud;
	}

	const int32 Width = static_cast<int32>(Info.Width);
	const int32 Height = static_cast<int32>(Info.Height);
	const int32 TotalCells = Width * Height;

	if (ElevationDataMeters.Num() != TotalCells)
	{
		return Cloud;
	}

	TArray<FVector3f> Points;
	Points.Reserve(TotalCells);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = Y * Width + X;
			const float ElevationMeters = ElevationDataMeters[Index];

			if (FMath::IsNaN(ElevationMeters))
			{
				continue;
			}

			const float RosX = static_cast<float>(
				Info.OriginX + (static_cast<double>(X) + 0.5) * Info.ResolutionMeters);

			const float RosY = static_cast<float>(
				Info.OriginY + (static_cast<double>(Y) + 0.5) * Info.ResolutionMeters);

			Points.Add(FVector3f(RosX, RosY, ElevationMeters));
		}
	}

	Cloud.height = 1;
	Cloud.width = static_cast<uint32>(Points.Num());

	Cloud.is_bigendian = false;
	Cloud.is_dense = false;

	Cloud.point_step = 12; // x, y, z as float32
	Cloud.row_step = Cloud.point_step * Cloud.width;

	AddFloatField(Cloud, "x", 0);
	AddFloatField(Cloud, "y", 4);
	AddFloatField(Cloud, "z", 8);

	const int32 DataSize = static_cast<int32>(Cloud.row_step);
	TArray<uint8> Data;
	Data.SetNumZeroed(DataSize);

	for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
	{
		const int32 BaseOffset = PointIndex * static_cast<int32>(Cloud.point_step);

		WriteFloat(Data, BaseOffset + 0, Points[PointIndex].X);
		WriteFloat(Data, BaseOffset + 4, Points[PointIndex].Y);
		WriteFloat(Data, BaseOffset + 8, Points[PointIndex].Z);
	}

	Cloud.data.resize(static_cast<size_t>(Data.Num()));

	if (Data.Num() > 0)
	{
		FMemory::Memcpy(Cloud.data.data(), Data.GetData(), Data.Num());
	}

	return Cloud;
}
