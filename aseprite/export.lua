local spr = app.activeSprite
if not spr then return print('No active sprite') end

local local_path,title = spr.filename:match("^(.+[/\\])(.-).([^.]*)$")

-- CHANGE this to whatever your local path for the game is
local export_path = "C:/s/Untitled-Dungeon-Game/res/sprites"
local_path = export_path

function layer_export()
  local fn = local_path .. "/" .. app.activeLayer.name
  app.command.ExportSpriteSheet{
      ui=false,
      type=SpriteSheetType.HORIZONTAL,
      textureFilename=fn .. '.png',
      dataFormat=SpriteSheetDataFormat.JSON_ARRAY,
      layer=app.activeLayer.name,
      trim=true,
  }
end

layer_export()
