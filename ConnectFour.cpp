/*
* (C) 2023 badasahog. All Rights Reserved
* The above copyright notice shall be included in
* all copies or substantial portions of the Software.
*/

#include <Windows.h>
#include <wrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <sstream>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

#if !_HAS_CXX20
#error C++20 is required
#endif

#if !__has_include(<Windows.h>)
#error critital header Windows.h not found
#endif

HWND Window;

inline void FATAL_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			nullptr
		);

		wchar_t buffer[256];

		if (formattedErrorLength == 0)
		{
			_snwprintf_s(buffer, 256, _TRUNCATE, L"an error occured, unable to retrieve error message\nerror code: 0x%X\nlocation: line %i\n\0", hr, line);
			ShowWindow(Window, SW_HIDE);
			MessageBoxW(Window, buffer, L"Fatal Error", MB_OK | MB_SYSTEMMODAL | MB_ICONERROR);
		}
		else
		{
			_snwprintf_s(buffer, 256, _TRUNCATE, L"an error occured: %s\nerror code: 0x%X\nlocation: line %i\n\0", messageBuffer, hr, line);
			ShowWindow(Window, SW_HIDE);
			MessageBoxW(Window, buffer, L"Fatal Error", MB_OK | MB_SYSTEMMODAL | MB_ICONERROR);
		}
		ExitProcess(EXIT_FAILURE);
	}
}

#define FATAL_ON_FAIL(x) FATAL_ON_FAIL_IMPL(x, __LINE__)

#define FATAL_ON_FALSE(x) if((x) == FALSE) FATAL_ON_FAIL(GetLastError())

#define VALIDATE_HANDLE(x) if((x) == nullptr || (x) == INVALID_HANDLE_VALUE) FATAL_ON_FAIL(GetLastError())

using Microsoft::WRL::ComPtr;

ComPtr<ID2D1Factory> factory;
ComPtr<ID2D1HwndRenderTarget> renderTarget;

ComPtr<ID2D1SolidColorBrush> brush;
ComPtr<ID2D1SolidColorBrush> PlayerBrush;
ComPtr<ID2D1SolidColorBrush> CPUBrush;
ComPtr<ID2D1SolidColorBrush> GhostBrush;
ComPtr<ID2D1SolidColorBrush> PlayerWinBrush;
ComPtr<ID2D1SolidColorBrush> CPUWinBrush;

ComPtr<ID2D1PathGeometry> boardShape;


ComPtr<IDWriteFactory> pDWriteFactory;

ComPtr<IDWriteTextFormat> TitleTextFormat;
ComPtr<IDWriteTextFormat> MainTextFormat;
ComPtr<IDWriteTextFormat> CopyrightTextFormat;


LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept;


char boardState[7][6];

bool mouseClicked = false;

int gameState = 0;

int playerScore = 0;
int CPUScore = 0;

bool hilightWinningPieces = false;

char fallingPieceColor = 0;
int fallingPieceX;
float fallingPiecePosY;
int fallingPieceTargetY;

LARGE_INTEGER GameFinishedTicks;
LARGE_INTEGER CurrentTimerFinished;
LARGE_INTEGER TickCountPrevious;
LARGE_INTEGER FallingPieceSpeed;

int windowWidth = 0;
int windowHeight = 0;

bool bGeometryIsValid = false;

[[nodiscard]]
bool CheckForWinner(int lastMoveX, int lastMoveY) noexcept
{
	char objectiveType = boardState[lastMoveX][lastMoveY];

	//theoretical maximum of 20 piece win

	int winningPieces[20][2];
	int winningPieceCount = 0;

	bool winDetected = false;


	//vertical:
	{
		if (lastMoveY < 3)//make sure we're not checking out of bounds
		{
			if (boardState[lastMoveX][lastMoveY + 1] == objectiveType &&
				boardState[lastMoveX][lastMoveY + 2] == objectiveType &&
				boardState[lastMoveX][lastMoveY + 3] == objectiveType)
			{
				winningPieces[winningPieceCount][0] = lastMoveX;
				winningPieces[winningPieceCount][1] = lastMoveY + 1;
				winningPieceCount++;

				winningPieces[winningPieceCount][0] = lastMoveX;
				winningPieces[winningPieceCount][1] = lastMoveY + 2;
				winningPieceCount++;

				winningPieces[winningPieceCount][0] = lastMoveX;
				winningPieces[winningPieceCount][1] = lastMoveY + 3;
				winningPieceCount++;

				winDetected = true;
			}
		}
	}

	//horizontal:
	{
		int winningPiecesBookmark = winningPieceCount;
		int sequentialPieces = 0;

		//left
		for (int i = 1; i < lastMoveX + 1; i++)
		{
			if (boardState[lastMoveX - i][lastMoveY] == objectiveType)
			{
				winningPieces[winningPieceCount][0] = lastMoveX - i;
				winningPieces[winningPieceCount][1] = lastMoveY;
				
				winningPieceCount++;
				sequentialPieces++;
			}
			else
			{
				break;
			}
		}

		//right
		for (int i = 1; i < 6 - lastMoveX + 1; i++)
		{
			if (boardState[lastMoveX + i][lastMoveY] == objectiveType)
			{
				winningPieces[winningPieceCount][0] = lastMoveX + i;
				winningPieces[winningPieceCount][1] = lastMoveY;

				winningPieceCount++;
				sequentialPieces++;
			}
			else
			{
				break;
			}
		}

		if (sequentialPieces >= 3)
		{
			winDetected = true;
		}
		else
		{
			winningPieceCount = winningPiecesBookmark;
		}
	}

	// "\"
	{
		int winningPiecesBookmark = winningPieceCount;
		int sequentialPieces = 0;

		{
			//upward scan
			int scanLength = min(lastMoveX, lastMoveY);

			for (int i = 0; i < scanLength; i++)
			{
				if (boardState[lastMoveX - (i + 1)][lastMoveY - (i + 1)] == objectiveType)
				{
					winningPieces[winningPieceCount][0] = lastMoveX - (i + 1);
					winningPieces[winningPieceCount][1] = lastMoveY - (i + 1);

					winningPieceCount++;
					sequentialPieces++;
				}
				else
				{
					break;
				}
			}
		}

		{
			//downward scan
			int scanLength = min(6 - lastMoveX, 5 - lastMoveY);

			for (int i = 0; i < scanLength; i++)
			{
				if (boardState[lastMoveX + (i + 1)][lastMoveY + (i + 1)] == objectiveType)
				{
					winningPieces[winningPieceCount][0] = lastMoveX + (i + 1);
					winningPieces[winningPieceCount][1] = lastMoveY + (i + 1);

					winningPieceCount++;
					sequentialPieces++;
				}
				else
				{
					break;
				}
			}
		}

		if (sequentialPieces >= 3)
		{
			winDetected = true;
		}
		else
		{
			winningPieceCount = winningPiecesBookmark;
		}
	}
	
	// "/"
	{
		int winningPiecesBookmark = winningPieceCount;
		int sequentialPieces = 0;

		{
			//upward scan
			int scanLength = min(6 - lastMoveX, lastMoveY);

			for (int i = 0; i < scanLength; i++)
			{
				if (boardState[lastMoveX + (i + 1)][lastMoveY - (i + 1)] == objectiveType)
				{
					winningPieces[winningPieceCount][0] = lastMoveX + (i + 1);
					winningPieces[winningPieceCount][1] = lastMoveY - (i + 1);

					winningPieceCount++;
					sequentialPieces++;
				}
				else
				{
					break;
				}
			}
		}

		{
			//downward scan
			int scanLength = min(lastMoveX, 5 - lastMoveY);

			for (int i = 0; i < scanLength; i++)
			{
				if (boardState[lastMoveX - (i + 1)][lastMoveY + (i + 1)] == objectiveType)
				{
					winningPieces[winningPieceCount][0] = lastMoveX - (i + 1);
					winningPieces[winningPieceCount][1] = lastMoveY + (i + 1);

					winningPieceCount++;
					sequentialPieces++;
				}
				else
				{
					break;
				}
			}
		}

		if (sequentialPieces >= 3)
		{
			winDetected = true;
		}
		else
		{
			winningPieceCount = winningPiecesBookmark;
		}
	}

	if (winDetected)
	{
		for (int i = 0; i < winningPieceCount; i++)
		{
			boardState[winningPieces[i][0]][winningPieces[i][1]] = objectiveType + 2;
		}

		boardState[lastMoveX][lastMoveY] = objectiveType + 2;

		return true;
	}

	return false;
}

void CreateAssets() noexcept
{
	RECT ClientRect;
	FATAL_ON_FALSE(GetClientRect(Window, &ClientRect));

	D2D1_SIZE_U size = D2D1::SizeU(ClientRect.right, ClientRect.bottom);

	FATAL_ON_FAIL(factory->CreateHwndRenderTarget(
		D2D1::RenderTargetProperties(),
		D2D1::HwndRenderTargetProperties(Window, size),
		&renderTarget));

	renderTarget->SetDpi(96, 96);

	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 0.0f), &brush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 1.0f), &PlayerBrush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 0.0f), &CPUBrush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(.564f, .564f, .564f), &GhostBrush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 1.0f), &PlayerWinBrush));
	FATAL_ON_FAIL(renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.5f, 0.5f), &CPUWinBrush));

	bGeometryIsValid = false;
	
	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.12f * windowHeight,
		L"en-us",
		&TitleTextFormat
	));

	FATAL_ON_FAIL(TitleTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));

	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.08f * windowHeight,
		L"en-us",
		&MainTextFormat
	));

	FATAL_ON_FAIL(MainTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));

	FATAL_ON_FAIL(pDWriteFactory->CreateTextFormat(
		L"Segoe UI",
		NULL,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		.05f * windowHeight,
		L"en-us",
		&CopyrightTextFormat
	));

	FATAL_ON_FAIL(CopyrightTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
}

void DrawMenu() noexcept
{
	if (renderTarget == nullptr)
	{
		CreateAssets();
	}

	renderTarget->BeginDraw();
	renderTarget->Clear();

	{
		//title
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .1f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"CONNECT FOUR", 12, TitleTextFormat.Get(), textArea, PlayerBrush.Get());
	}

	POINT cursorPos;
	FATAL_ON_FALSE(GetCursorPos(&cursorPos));
	FATAL_ON_FALSE(ScreenToClient(Window, &cursorPos));

	{
		//play button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .3f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"PLAY", 4, MainTextFormat.Get(), textArea, GhostBrush.Get());
	}

	{
		//exit button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .45f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"EXIT", 4, MainTextFormat.Get(), textArea, GhostBrush.Get());
	}

	{
		//copyright
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .9f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * 1.f
		};
		renderTarget->DrawTextW(L"\u24B8 2023 badasahog. All Rights Reserved", 37, CopyrightTextFormat.Get(), textArea, GhostBrush.Get());
	}

	if (cursorPos.x > windowWidth * .4f &&
		cursorPos.x < windowWidth * .6f &&
		cursorPos.y > windowHeight * .3f &&
		cursorPos.y < windowHeight * .4f)
	{
		//play button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .3f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};

		renderTarget->DrawTextW(L"PLAY", 4, MainTextFormat.Get(), textArea, brush.Get());

		if (mouseClicked)
		{
			gameState = 1;
		}
	}
	else if (
		cursorPos.x > windowWidth * .4f &&
		cursorPos.x < windowWidth * .6f &&
		cursorPos.y > windowHeight * .45f &&
		cursorPos.y < windowHeight * .55f)
	{
		//exit button
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = windowHeight * .45f,
			.right = (FLOAT)windowWidth,
			.bottom = windowHeight * .8f
		};
		renderTarget->DrawTextW(L"EXIT", 4, MainTextFormat.Get(), textArea, brush.Get());

		if (mouseClicked)
		{
			ExitProcess(EXIT_SUCCESS);
		}
	}

	mouseClicked = false;

	FATAL_ON_FAIL(renderTarget->EndDraw());
}

void DrawGame() noexcept
{

	if (renderTarget == nullptr)
	{
		CreateAssets();
	}

	renderTarget->BeginDraw();
	renderTarget->Clear();

	FLOAT ScoreWidth = .2 * windowWidth;

	{
		D2D1_RECT_F textArea =
		{
			.left = 0,
			.top = 0,
			.right = (FLOAT)windowWidth / 2 - ScoreWidth,
			.bottom = (.1f / .5f) * windowHeight
		};
		renderTarget->DrawTextW(L"YOU", 3, MainTextFormat.Get(), textArea, PlayerBrush.Get());
	}

	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2 - ScoreWidth,
			.top = 0,
			.right = (FLOAT)windowWidth / 2,
			.bottom = (.1f / .5f) * windowHeight
		};

		std::wstring scoreText = std::to_wstring(playerScore);
		renderTarget->DrawTextW(scoreText.c_str(), scoreText.length(), MainTextFormat.Get(), textArea, PlayerBrush.Get());
	}


	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2 + ScoreWidth,
			.top = 0,
			.right = (FLOAT)windowWidth,
			.bottom = (.1f / .5f) * windowHeight
		};
		renderTarget->DrawTextW(L"CPU", 3, MainTextFormat.Get(), textArea, CPUBrush.Get());
	}

	{
		D2D1_RECT_F textArea =
		{
			.left = (FLOAT)windowWidth / 2,
			.top = 0,
			.right = (FLOAT)windowWidth / 2 + ScoreWidth,
			.bottom = (.1f / .5f) * windowHeight
		};

		std::wstring scoreText = std::to_wstring(CPUScore);
		renderTarget->DrawTextW(scoreText.c_str(), scoreText.length(), MainTextFormat.Get(), textArea, CPUBrush.Get());
	}

	float boardMarginsHorizontal = (FLOAT)windowWidth * .1f;

	float boardWidth = (FLOAT)windowWidth - boardMarginsHorizontal * 2;

	float c4SquareSize = boardWidth / 7.f;

	float boardMarginTop = (FLOAT)windowWidth * .25f;

	float boardHeight = c4SquareSize * 6;


	D2D1_RECT_F boardRect =
	{
		.left = boardMarginsHorizontal,
		.top = boardMarginTop,
		.right = boardMarginsHorizontal + boardWidth,
		.bottom = boardMarginTop + boardHeight
	};

	if (!bGeometryIsValid)
	{
		ComPtr<ID2D1RectangleGeometry> boundingSquare;

		FATAL_ON_FAIL(factory->CreateRectangleGeometry(boardRect, &boundingSquare));

		ID2D1EllipseGeometry* cutoutCircle[6 * 7];

		for (int x = 0; x < 7; x++)
		{
			for (int y = 0; y < 6; y++)
			{
				D2D1_ELLIPSE ellipse =
				{
					.point =
					{
						.x = boardRect.left + c4SquareSize * x + c4SquareSize / 2,
						.y = boardRect.top + c4SquareSize * y + c4SquareSize / 2
					},
					.radiusX = (c4SquareSize / 2) * .8f,
					.radiusY = (c4SquareSize / 2) * .8f
				};
				
				FATAL_ON_FAIL(factory->CreateEllipseGeometry(ellipse, &cutoutCircle[x * 6 + y]));
			}
		}

		ComPtr<ID2D1GeometryGroup> geometryGroup;

		FATAL_ON_FAIL(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, (ID2D1Geometry**)&cutoutCircle[0], 6 * 7, &geometryGroup));


		FATAL_ON_FAIL(factory->CreatePathGeometry(&boardShape));

		ID2D1GeometrySink* GeometrySink;
		FATAL_ON_FAIL(boardShape->Open(&GeometrySink));


		FATAL_ON_FAIL(boundingSquare->CombineWithGeometry(geometryGroup.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, GeometrySink));


		FATAL_ON_FAIL(GeometrySink->Close());

		for (int i = 0; i < 6 * 7; i++)
		{
			cutoutCircle[i]->Release();
		}

		bGeometryIsValid = true;
	}


	//draw the pieces
	for (int x = 0; x < 7; x++)
	{
		for (int y = 0; y < 6; y++)
		{
			if (boardState[x][y] != 0)
			{
				D2D1_RECT_F rect =
				{
					.left = boardMarginsHorizontal + c4SquareSize * x,
					.top = boardMarginTop + c4SquareSize * y,
					.right = boardMarginsHorizontal + c4SquareSize * (x + 1),
					.bottom = boardMarginTop + c4SquareSize * (y + 1)
				};

				if (hilightWinningPieces)
				{
					if (boardState[x][y] == 1)//blue
						renderTarget->FillRectangle(rect, PlayerBrush.Get());
					else if (boardState[x][y] == 2)//red
						renderTarget->FillRectangle(rect, CPUBrush.Get());
					else if (boardState[x][y] == 3)//hilighted blue
						renderTarget->FillRectangle(rect, PlayerWinBrush.Get());
					else if (boardState[x][y] == 4)//hilighted red
						renderTarget->FillRectangle(rect, CPUWinBrush.Get());
				}
				else
				{
					if (boardState[x][y] == 1 || boardState[x][y] == 3)//blue
						renderTarget->FillRectangle(rect, PlayerBrush.Get());
					else if (boardState[x][y] == 2 || boardState[x][y] == 4)//red
						renderTarget->FillRectangle(rect, CPUBrush.Get());
				}
			}
		}
	}

	if (gameState == 1)
	{
		POINT cursorPos;
		FATAL_ON_FALSE(GetCursorPos(&cursorPos));
		FATAL_ON_FALSE(ScreenToClient(Window, &cursorPos));


		if (cursorPos.x > boardRect.left && cursorPos.x < boardRect.right)
		{
			int boardColumn = (cursorPos.x - boardRect.left) / c4SquareSize;

			D2D1_ELLIPSE playerPiece =
			{
				.point =
				{
					.x = boardMarginsHorizontal + c4SquareSize * boardColumn + c4SquareSize / 2,
					.y = boardMarginTop - c4SquareSize + c4SquareSize / 2
				},
				.radiusX = (c4SquareSize / 2) * .85f,
				.radiusY = (c4SquareSize / 2) * .85f
			};
			renderTarget->FillEllipse(playerPiece, PlayerBrush.Get());

			if (mouseClicked)
			{
				for (int i = 0; i < 6; i++)
				{
					if (boardState[boardColumn][5 - i] == 0)
					{

						fallingPieceColor = 1;
						fallingPiecePosY = boardMarginTop - c4SquareSize + c4SquareSize / 2;
						fallingPieceX = boardColumn;
						fallingPieceTargetY = 5 - i;
						gameState = 3;
						break;
					}
				}
			}
		}
	}
	else if (gameState == 2)
	{
		char availableColumns[7] = { 0 };
		int numAvailableMoves = 0;

		for (int i = 0; i < 7; i++)
		{
			if (boardState[i][0] == 0)
			{
				availableColumns[numAvailableMoves] = i;
				numAvailableMoves++;
			}
		}

		if (numAvailableMoves == 0)
			gameState = 4;

		int boardColumn = availableColumns[rand() % numAvailableMoves];

		for (int i = 0; i < 6; i++)
		{
			if (boardState[boardColumn][5 - i] == 0)
			{
				fallingPieceColor = 2;
				fallingPiecePosY = boardMarginTop - c4SquareSize + c4SquareSize / 2;
				fallingPieceX = boardColumn;
				fallingPieceTargetY = 5 - i;
				gameState = 3;
				break;
			}
		}
	}
	else if (gameState == 3)
	{
		D2D1_ELLIPSE playerPiece =
		{
			.point =
			{
				.x = boardMarginsHorizontal + c4SquareSize * fallingPieceX + c4SquareSize / 2,
				.y = fallingPiecePosY
			},
			.radiusX = (c4SquareSize / 2) * .85f,
			.radiusY = (c4SquareSize / 2) * .85f
		};

		if (fallingPieceColor == 1)
		{
			renderTarget->FillEllipse(playerPiece, PlayerBrush.Get());
		}
		else
		{
			renderTarget->FillEllipse(playerPiece, CPUBrush.Get());
		}

		LARGE_INTEGER tickCountNow;
		FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));

		if (TickCountPrevious.QuadPart != 0)
		{
			fallingPiecePosY += ((tickCountNow.QuadPart - TickCountPrevious.QuadPart) / (float)FallingPieceSpeed.QuadPart) * windowHeight;
		}

		TickCountPrevious.QuadPart = tickCountNow.QuadPart;
		

		if (fallingPiecePosY > (boardMarginTop + c4SquareSize * (fallingPieceTargetY + 1) - c4SquareSize / 2))
		{
			boardState[fallingPieceX][fallingPieceTargetY] = fallingPieceColor;

			if (CheckForWinner(fallingPieceX, fallingPieceTargetY))
			{
				gameState = 4;

				CurrentTimerFinished.QuadPart = tickCountNow.QuadPart + GameFinishedTicks.QuadPart;

				if (fallingPieceColor == 1)
				{
					playerScore++;
				}
				else
				{
					CPUScore++;
				}
			}
			else
			{
				if (fallingPieceColor == 1)
				{
					gameState = 2;
				}
				else
				{
					gameState = 1;
				}
			}

			TickCountPrevious.QuadPart = 0;
		}
	}
	else if (gameState == 4)
	{
		LARGE_INTEGER tickCountNow;
		FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));

		if(fmod((CurrentTimerFinished.QuadPart - tickCountNow.QuadPart) / (float)GameFinishedTicks.QuadPart, .15f) < .075f)
			hilightWinningPieces = true;
		else
			hilightWinningPieces = false;

		if (CurrentTimerFinished.QuadPart < tickCountNow.QuadPart)
		{
			hilightWinningPieces = false;
			gameState = 1;
			memset(boardState, 0, sizeof(boardState));
		}
	}

	//draw the board

	renderTarget->FillGeometry(boardShape.Get(), brush.Get());

	mouseClicked = false;

	FATAL_ON_FAIL(renderTarget->EndDraw());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
	LARGE_INTEGER ProcessorFrequency;
	FATAL_ON_FALSE(QueryPerformanceFrequency(&ProcessorFrequency));

	GameFinishedTicks.QuadPart = ProcessorFrequency.QuadPart * 1;

	FallingPieceSpeed.QuadPart = ProcessorFrequency.QuadPart * .5;

	{
		LARGE_INTEGER tickCountNow;
		FATAL_ON_FALSE(QueryPerformanceCounter(&tickCountNow));
		srand(tickCountNow.LowPart);
	}

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	UINT dpi = GetDpiForSystem();

	windowWidth = 6 * dpi;
	windowHeight = 6 * dpi;

	// Register the window class.
	constexpr wchar_t CLASS_NAME[] = L"Window CLass";

	WNDCLASS wc =
	{
		.lpfnWndProc = PreInitProc,
		.hInstance = hInstance,
		.lpszClassName = CLASS_NAME
	};
	RegisterClassW(&wc);

	// Get the required window size
	RECT windowRect =
	{
		.left = 50,
		.top = 50,
		.right = windowWidth + 50,
		.bottom = windowHeight + 50
	};

	FATAL_ON_FALSE(AdjustWindowRect(&windowRect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE));


	Window = CreateWindowExW(
		WS_EX_CLIENTEDGE,
		CLASS_NAME,
		L"Connect Four",
		WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		(GetSystemMetrics(SM_CXSCREEN) - (windowRect.right - windowRect.left)) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - (windowRect.bottom - windowRect.top)) / 2,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	VALIDATE_HANDLE(Window);

	FATAL_ON_FAIL(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));

	FATAL_ON_FAIL(DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		&pDWriteFactory
	));


	FATAL_ON_FALSE(ShowWindow(Window, SW_SHOW));


	SetWindowLongPtrA(Window, GWLP_WNDPROC, (LONG_PTR)&WindowProc);

	SetCursor(LoadCursorW(NULL, IDC_ARROW));

	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
		{
			FATAL_ON_FALSE(TranslateMessage(&Message));
			DispatchMessageW(&Message);
		}
	}

	return EXIT_SUCCESS;
}

void handleDpiChange() noexcept
{
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	UINT dpi = GetDpiForSystem();

	windowWidth = 6 * dpi;
	windowHeight = 6 * dpi;

	RECT windowRect =
	{
		.left = 50,
		.top = 50,
		.right = windowWidth + 50,
		.bottom = windowHeight + 50
	};

	FATAL_ON_FALSE(AdjustWindowRect(&windowRect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE));

	FATAL_ON_FALSE(SetWindowPos(
		Window,
		nullptr,
		0,
		0,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOOWNERZORDER | SWP_NOREPOSITION | SWP_NOSENDCHANGING));
}

LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (message)
	{
	case WM_DPICHANGED:
		handleDpiChange();
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (message)
	{
	case WM_DPICHANGED:
		handleDpiChange();
		break;
	case WM_PAINT:
		Sleep(25);
		break;
	case WM_SIZE:
		if (!IsIconic(hwnd))
			FATAL_ON_FALSE(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)&WindowProc) != 0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
		mouseClicked = true;
		break;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			gameState = 0;

			memset(boardState, 0, sizeof(boardState));

			hilightWinningPieces = false;
			playerScore = 0;
			CPUScore = 0;
			mouseClicked = false;
		}
		break;
	case WM_DPICHANGED:
		handleDpiChange();
		[[fallthrough]];
	case WM_SIZE:
		if (IsIconic(hwnd))
		{
			FATAL_ON_FALSE(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)&IdleProc) != 0);
			break;
		}
		CreateAssets();
		[[fallthrough]];
	case WM_PAINT:
		if (gameState == 0)
			DrawMenu();
		else
			DrawGame();
		break;
	default:
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	}
	return 0;
}