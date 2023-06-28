function OnInit()
{	
	//-----Test-----
	//Set_RecentFile_MouseRightBtn_Event();
	
	
	//-----Official-----
    TranslatePage();

	SendMsg_GetLoginInfo();
	SendMsg_GetRecentFile();
}

function GotoMenu( strMenu )
{
	let MenuList = $(".BtnItem");
	let nAll=MenuList.length;
	
	for(let n=0;n<nAll;n++)
	{
		let OneBtn=MenuList[n];
		
		if( $(OneBtn).attr("menu")==strMenu )
		{
			$(".BtnItem").removeClass("BtnItemSelected");			
			
			$(OneBtn).addClass("BtnItemSelected");
			
			$("div[board]").hide();
			$("div[board=\'"+strMenu+"\']").show();
		}
	}
}

function GotoMain(strMenu) {
	let MenuList = $(".TbItem")
	let nAll = MenuList.length;

	for (let n = 0; n < nAll; n++) {
		let OneBtn = MenuList[n];

		if ($(OneBtn).attr("menu") == strMenu) {
			$(".TbItem").removeClass("TbItemSelected");

			$(OneBtn).addClass("TbItemSelected");
			$(".BtnItem").removeClass("BtnItemSelected");

			$("div[board]").hide();
			$("div[board=\'" + strMenu + "\']").show();
		}
	}
}

//---------------Global-----------------
window.postMessage = HandleStudio;

