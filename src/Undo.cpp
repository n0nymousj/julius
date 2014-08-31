#include "Undo.h"

#include "Grid.h"
#include "Resource.h"
#include "Routing.h"
#include "Terrain.h"
#include "TerrainGraphics.h"
#include "UI/Window.h"

#include "Data/Building.h"
#include "Data/CityInfo.h"
#include "Data/Constants.h"
#include "Data/Event.h"
#include "Data/Graphics.h"
#include "Data/Grid.h"
#include "Data/Settings.h"
#include "Data/State.h"

#include <string.h>

#define MAX_UNDO_BUILDINGS 50

static struct {
	int timeout;
	int buildingCost;
	int numBuildings;
	int buildingType;
	struct Data_Building buildings[MAX_UNDO_BUILDINGS];
	short buildingIndex[MAX_UNDO_BUILDINGS];
} data;

static void clearBuildingList()
{
	data.numBuildings = 0;
	memset(data.buildings, 0, MAX_UNDO_BUILDINGS * sizeof(struct Data_Building));
	memset(data.buildingIndex, 0, MAX_UNDO_BUILDINGS * sizeof(short));
}

int Undo_recordBeforeBuild()
{
	Data_State.undoReady = 0;
	Data_State.undoAvailable = 1;
	data.timeout = 0;
	data.buildingCost = 0;
	data.buildingType = Data_State.selectedBuilding.type;
	clearBuildingList();
	for (int i = 1; i < MAX_BUILDINGS; i++) {
		if (Data_Buildings[i].inUse == 2) {
			Data_State.undoAvailable = 0;
			return 0;
		}
		if (Data_Buildings[i].inUse == 6) {
			Data_State.undoAvailable = 0;
		}
	}

	Grid_copyShortGrid(Data_Grid_graphicIds, Data_Grid_Undo_graphicIds);
	Grid_copyShortGrid(Data_Grid_terrain, Data_Grid_Undo_terrain);
	Grid_copyByteGrid(Data_Grid_aqueducts, Data_Grid_Undo_aqueducts);
	Grid_copyByteGrid(Data_Grid_bitfields, Data_Grid_Undo_bitfields);
	Grid_copyByteGrid(Data_Grid_edge, Data_Grid_Undo_edge);
	Grid_copyByteGrid(Data_Grid_spriteOffsets, Data_Grid_Undo_spriteOffsets);

	return 1;
}

void Undo_restoreBuildings()
{
	for (int i = 0; i < data.numBuildings; i++) {
		if (data.buildingIndex[i]) {
			struct Data_Building *b = &Data_Buildings[data.buildingIndex[i]];
			if (b->inUse == 6) {
				b->inUse = 1;
			}
			b->isDeleted = 0;
		}
	}
	clearBuildingList();
}

void Undo_addBuildingToList(int buildingId)
{
	if (buildingId <= 0) {
		return;
	}
	data.numBuildings = 0;
	int isOnList = 0;
	for (int i = 0; i < MAX_UNDO_BUILDINGS; i++) {
		if (data.buildingIndex[i]) {
			data.numBuildings++;
		}
		if (data.buildingIndex[i] == buildingId) {
			isOnList = 1;
		}
	}
	if (!isOnList) {
		for (int i = 0; i < MAX_UNDO_BUILDINGS; i++) {
			if (!data.buildingIndex[i]) {
				data.numBuildings++;
				memcpy(&data.buildings[i], &Data_Buildings[buildingId], sizeof(struct Data_Building));
				data.buildingIndex[i] = buildingId;
				return;
			}
		}
		Data_State.undoAvailable = 0;
	}
}

int Undo_isBuildingInList(int buildingId)
{
	if (buildingId <= 0 || Data_State.undoReady != 1 || Data_State.undoAvailable != 1) {
		return 0;
	}
	if (data.numBuildings <= 0) {
		return 0;
	}
	for (int i = 0; i < MAX_UNDO_BUILDINGS; i++) {
		if (data.buildingIndex[i] == buildingId) {
			return 1;
		}
	}
	return 0;
}

void Undo_recordBuild(int cost)
{
	Data_State.undoReady = 1;
	data.timeout = 500;
	data.buildingCost = cost;
	UI_Window_requestRefresh();
}

void Undo_restoreTerrainGraphics()
{
	for (int y = 0; y < Data_Settings_Map.height; y++) {
		for (int x = 0; x < Data_Settings_Map.width; x++) {
			int gridOffset = GridOffset(x, y);
			if (!Data_Grid_buildingIds[gridOffset]) {
				Data_Grid_graphicIds[gridOffset] = Data_Grid_Undo_graphicIds[gridOffset];
			}
		}
	}
}

static void placeBuildingOnTerrain(int buildingId)
{
	if (buildingId <= 0) {
		return;
	}
	struct Data_Building *b = &Data_Buildings[buildingId];
	if (BuildingIsFarm(b->type)) {
		int graphicOffset;
		switch (b->type) {
			default:
			case Building_WheatFarm: graphicOffset = 0; break;
			case Building_VegetableFarm: graphicOffset = 5; break;
			case Building_FruitFarm: graphicOffset = 10; break;
			case Building_OliveFarm: graphicOffset = 15; break;
			case Building_VinesFarm: graphicOffset = 20; break;
			case Building_PigFarm: graphicOffset = 25; break;
		}
		TerrainGraphics_setBuildingFarm(buildingId, b->x, b->y,
			GraphicId(ID_Graphic_FarmCrops) + graphicOffset, 0);
	} else {
		int size = Constant_BuildingProperties[buildingId].size;
		Terrain_addBuildingToGrids(buildingId, b->x, b->y, size, 0, 0);
		if (b->type == Building_Wharf) {
			b->data.other.boatWalkerId = 0;
		}
	}
	b->inUse = 1;
}

void Undo_perform()
{
	if (!Data_State.undoReady || !Data_State.undoAvailable) {
		return;
	}
	Data_State.undoAvailable = 0;
	Data_CityInfo.treasury += data.buildingCost;
	Data_CityInfo.financeConstructionThisYear -= data.buildingCost;
	if (data.buildingType == Building_ClearLand) {
		for (int i = 0; i < data.numBuildings; i++) {
			if (data.buildingIndex[i]) {
				int buildingId = data.buildingIndex[i];
				memcpy(&Data_Buildings[buildingId], &data.buildings[i],
					sizeof(struct Data_Building));
				placeBuildingOnTerrain(buildingId);
			}
		}
		Grid_copyShortGrid(Data_Grid_Undo_terrain, Data_Grid_terrain);
		Grid_copyByteGrid(Data_Grid_Undo_aqueducts, Data_Grid_aqueducts);
		Grid_copyByteGrid(Data_Grid_Undo_spriteOffsets, Data_Grid_spriteOffsets);
		Grid_copyShortGrid(Data_Grid_Undo_graphicIds, Data_Grid_graphicIds);
		Grid_copyByteGrid(Data_Grid_Undo_bitfields, Data_Grid_bitfields);
		Grid_copyByteGrid(Data_Grid_Undo_edge, Data_Grid_edge);
		Grid_andByteGrid(Data_Grid_bitfields, 0xaf);
	} else if (data.buildingType == Building_Aqueduct || data.buildingType == Building_Road ||
			data.buildingType == Building_Wall) {
		Grid_copyShortGrid(Data_Grid_Undo_terrain, Data_Grid_terrain);
		Grid_copyByteGrid(Data_Grid_Undo_aqueducts, Data_Grid_aqueducts);
		Undo_restoreTerrainGraphics();
	} else if (data.buildingType == Building_LowBridge || data.buildingType == Building_ShipBridge) {
		Grid_copyShortGrid(Data_Grid_Undo_terrain, Data_Grid_terrain);
		Grid_copyByteGrid(Data_Grid_Undo_spriteOffsets, Data_Grid_spriteOffsets);
		Undo_restoreTerrainGraphics();
	} else if (data.buildingType == Building_Plaza || data.buildingType == Building_Gardens) {
		Grid_copyShortGrid(Data_Grid_Undo_terrain, Data_Grid_terrain);
		Grid_copyByteGrid(Data_Grid_Undo_aqueducts, Data_Grid_aqueducts);
		Grid_copyByteGrid(Data_Grid_Undo_bitfields, Data_Grid_bitfields);
		Grid_copyByteGrid(Data_Grid_Undo_edge, Data_Grid_edge);
		Undo_restoreTerrainGraphics();
	} else if (data.numBuildings) {
		if (data.buildingType == Building_DraggableReservoir) {
			Grid_copyShortGrid(Data_Grid_Undo_terrain, Data_Grid_terrain);
			Grid_copyByteGrid(Data_Grid_Undo_aqueducts, Data_Grid_aqueducts);
			Undo_restoreTerrainGraphics();
		}
		for (int i = 0; i < data.numBuildings; i++) {
			if (data.buildingIndex[i]) {
				struct Data_Building *b = &Data_Buildings[data.buildingIndex[i]];
				if (b->type == Building_Oracle || (b->type >= Building_LargeTempleCeres && b->type <= Building_LargeTempleVenus)) {
					Resource_addToCityWarehouses(Resource_Marble, 2);
				}
				b->inUse = 2;
			}
		}
	}
	Routing_determineLandCitizen();
	Routing_determineLandNonCitizen();
	Routing_determineWalls();
	data.numBuildings = 0;
}

void Undo_updateAvailable()
{
	if (!Data_State.undoAvailable || !Data_State.undoReady) {
		return;
	}
	if (data.timeout <= 0 || Data_Event.earthquake.state == 1) {
		Data_State.undoAvailable = 0;
		clearBuildingList();
		UI_Window_requestRefresh();
		return;
	}
	data.timeout--;
	switch (data.buildingType) {
		case Building_ClearLand:
		case Building_Aqueduct:
		case Building_Road:
		case Building_Wall:
		case Building_LowBridge:
		case Building_ShipBridge:
		case Building_Plaza:
		case Building_Gardens:
			return;
	}
	if (data.numBuildings <= 0) {
		Data_State.undoAvailable = 0;
		UI_Window_requestRefresh();
		return;
	}
	if (data.buildingType == Building_HouseVacantLot) {
		for (int i = 0; i < data.numBuildings; i++) {
			if (data.buildingIndex[i] && Data_Buildings[data.buildingIndex[i]].housePopulation) {
				// no undo on a new house where people moved in
				Data_State.undoAvailable = 0;
				UI_Window_requestRefresh();
				return;
			}
		}
	}
	for (int i = 0; i < data.numBuildings; i++) {
		if (data.buildingIndex[i]) {
			struct Data_Building *b = &Data_Buildings[data.buildingIndex[i]];
			if (b->inUse == 2 || b->inUse == 4 || b->inUse == 5) {
				Data_State.undoAvailable = 0;
				UI_Window_requestRefresh();
				return;
			}
			if (b->type != data.buildings[i].type || b->gridOffset != data.buildings[i].gridOffset) {
				Data_State.undoAvailable = 0;
				UI_Window_requestRefresh();
				return;
			}
		}
	}
}