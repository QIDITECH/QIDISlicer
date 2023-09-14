function OnInit()
{
	TranslatePage();

	SendMsg_GetLoginInfo();
	SendMsg_GetRecentFile();
}

function GotoMenu( strMenu )
{
	if (strMenu == "UserGuide")
	{
		$(".MenuBtnSelected").removeClass("MenuBtnSelected");
		$("div[board]").hide();
		$("div[board=\'" + strMenu + "\']").show();
	}
	else
	{
		let MenuList = $(".MenuBtn");
		let nAll=MenuList.length;

		for(let n=0;n<nAll;n++)
		{
			let OneBtn=MenuList[n];
			if ($(OneBtn).attr("menu")==strMenu)
			{
				$(".MenuBtnSelected").removeClass("MenuBtnSelected");
				$(OneBtn).addClass("MenuBtnSelected");
				$("div[board]").hide();
				$("div[board=\'"+strMenu+"\']").show();
			}
		}
	}
}

//---------------Global-----------------
window.postMessage = HandleStudio;

